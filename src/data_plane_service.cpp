#include "data_plane_service.h"
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <iostream>

using namespace locality_messaging;

// ----------------------------
// Helpers
// ----------------------------
ChatEntry fromProto(const ChatMessage& m) {
    ChatEntry e;
    e.message_id   = m.message_id();
    e.sender_id    = m.sender_id();
    e.payload      = m.payload();
    e.lamport_time = m.lamport_time();
    e.epoch        = m.epoch();
    return e;
}

static LogSummary BuildSummary(const std::string& node_id,
                              const std::vector<ChatEntry>& entries,
                              uint32_t hash) {
    LogSummary summary;
    summary.set_node_id(node_id);
    summary.set_entry_count(static_cast<int32_t>(entries.size()));
    summary.set_log_hash(hash);

    int64_t max_lamport = 0;
    for (const auto& e : entries) {
        if (e.lamport_time > max_lamport)
            max_lamport = e.lamport_time;
    }
    summary.set_max_lamport_time(max_lamport);
    return summary;
}

// ----------------------------
// Constructor
// ----------------------------
DataPlaneGossipImpl::DataPlaneGossipImpl(
    const std::string& node_id,
    const std::string& auth_address)
    : node_id_(node_id), store_(node_id)
{
    auth_stub_ = IntegrationAuth::NewStub(
        grpc::CreateChannel(auth_address,
                            grpc::InsecureChannelCredentials()));

    std::cerr << "DataPlane node [" << node_id
              << "] auth backend at " << auth_address << std::endl;
}

// ----------------------------
// SyncLogs implementation
// ----------------------------
grpc::Status DataPlaneGossipImpl::SyncLogs(
    grpc::ServerContext* ctx,
    const SyncLogsRequest* req,
    SyncLogsResponse* resp)
{
    (void)ctx;

    std::cerr << "SyncLogs request received from "
              << req->sender().node_id() << std::endl;

    if (req->has_summary()) {
        std::cerr << "Incoming summary: entries="
                  << req->summary().entry_count()
                  << ", max_lamport="
                  << req->summary().max_lamport_time()
                  << std::endl;
    }

    std::vector<ChatEntry> incoming;
    incoming.reserve(req->logs_size());

    int accepted = 0;
    int rejected = 0;

    for (const auto& msg : req->logs()) {
        if (!checkAuth(msg.sender_id(), msg.epoch())) {
            rejected++;
            std::cerr << "Rejected message from " << msg.sender_id()
                      << " (epoch=" << msg.epoch() << ")" << std::endl;
            continue;
        }

        incoming.push_back(fromProto(msg));
        accepted++;
    }
    store_.tick();
    if (!incoming.empty()) {
        store_.merge(incoming);
    }
    

    const auto current_entries = store_.all();
    const uint32_t current_hash = store_.get_hash();

    resp->set_success(true);
    resp->set_receiver_lamport_time(store_.get_lamport_time());

    *resp->mutable_summary() =
        BuildSummary(node_id_, current_entries, current_hash);

    std::cerr << "SyncLogs from " << req->sender().node_id()
              << " accepted=" << accepted
              << " rejected=" << rejected
              << " local_lamport="
              << resp->receiver_lamport_time()
              << std::endl;

    return grpc::Status::OK;
} 

// ----------------------------
// Auth helper
// ----------------------------
bool DataPlaneGossipImpl::checkAuth(
    const std::string& user_id,
    int32_t epoch)
{
    AuthCheckRequest req;
    req.set_user_id(user_id);
    req.set_epoch(epoch);

    AuthCheckResponse resp;
    grpc::ClientContext ctx;

    auto status = auth_stub_->CheckAuthorization(&ctx, req, &resp);
    if (!status.ok())
        return false;

    return resp.is_authorized();
}