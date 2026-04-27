#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <grpc/grpc_security.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>
#include <grpcpp/create_channel.h>              
#include <grpcpp/security/credentials.h>       

#include "locality_messaging.grpc.pb.h"
#include "log_store.h"

using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using namespace locality_messaging;

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
        if (e.lamport_time > max_lamport) {
            max_lamport = e.lamport_time;
        }
    }
    summary.set_max_lamport_time(max_lamport);
    return summary;
}

class DataPlaneGossipImpl final : public DataPlaneGossip::Service {
public:
    DataPlaneGossipImpl(const std::string& node_id,
                        const std::string& auth_address)
        : node_id_(node_id), store_(node_id) {

        // create stub to talk to auth service
        auth_stub_ = IntegrationAuth::NewStub(
            grpc::CreateChannel(auth_address,
                                grpc::InsecureChannelCredentials()));

        std::cerr << "DataPlane node [" << node_id
                  << "] auth backend at " << auth_address << std::endl;
    }

    Status SyncLogs(ServerContext* ctx,
                    const SyncLogsRequest* req,
                    SyncLogsResponse* resp) override {
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

        size_t accepted = 0;
        size_t rejected = 0;

        for (const auto& msg : req->logs()) {
            // check with control-plane before accepting
            if (!checkAuth(msg.sender_id(), msg.epoch())) {
                ++rejected;
                std::cerr << "Rejected message from " << msg.sender_id()
                          << " (epoch=" << msg.epoch() << ")" << std::endl;
                continue;
            }
            incoming.push_back(fromProto(msg));
            ++accepted;
        }

        if (!incoming.empty()) {
            store_.merge(incoming);
        }

        const auto current_entries = store_.all();
        const uint32_t current_hash = store_.get_hash();

        resp->set_success(true);
        resp->set_receiver_lamport_time(store_.get_lamport_time());

        // Return summary so peer can compare state
        *resp->mutable_summary() =
            BuildSummary(node_id_, current_entries, current_hash);

        std::cerr << "SyncLogs from " << req->sender().node_id()
                  << " accepted=" << accepted
                  << " rejected=" << rejected
                  << " local_lamport="
                  << resp->receiver_lamport_time()
                  << std::endl;

        return Status::OK;
    }

private:
    std::string node_id_;
    LogStore store_;

    // Auth client stub
    std::unique_ptr<IntegrationAuth::Stub> auth_stub_;

    // call control-plane to validate user + epoch
    bool checkAuth(const std::string& user_id, int32_t epoch) {
        AuthCheckRequest req;
        req.set_user_id(user_id);
        req.set_epoch(epoch);

        AuthCheckResponse resp;
        grpc::ClientContext ctx;

        auto status = auth_stub_->CheckAuthorization(&ctx, req, &resp);
        if (!status.ok()) {
            std::cerr << "Auth RPC failed: "
                      << status.error_message() << std::endl;
            return false;
        }

        return resp.is_authorized();
    }
};

void RunServer(const std::string& node_id,
               const std::string& listen_address,
               const std::string& auth_address) {
    DataPlaneGossipImpl service(node_id, auth_address);

    ServerBuilder builder;
    builder.AddListeningPort(listen_address,
                             grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cerr << "Listening on " << listen_address << std::endl;
    server->Wait();
}

int main(int argc, char** argv) {
    std::string node_id     = argc > 1 ? argv[1] : "node-1";
    std::string listen_addr = argc > 2 ? argv[2] : "0.0.0.0:50051";
    std::string auth_addr   = argc > 3 ? argv[3] : "localhost:50053";

    RunServer(node_id, listen_addr, auth_addr);
    return 0;
}