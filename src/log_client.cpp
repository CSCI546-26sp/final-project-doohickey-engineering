#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "locality_messaging.grpc.pb.h"
#include "log_store.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using namespace locality_messaging;

// Helper function (correctly outside class)
static LogSummary BuildSummary(const std::string& node_id,
                               const std::vector<ChatEntry>& entries) {
    LogSummary summary;
    summary.set_node_id(node_id);
    summary.set_entry_count(static_cast<int32_t>(entries.size()));

    int64_t max_lamport = 0;
    uint32_t hash = 0;

    for (const auto& e : entries) {
        if (e.lamport_time > max_lamport) {
            max_lamport = e.lamport_time;
        }
        hash ^= static_cast<uint32_t>(std::hash<std::string>{}(e.payload));
    }

    summary.set_max_lamport_time(max_lamport);
    summary.set_log_hash(hash);
    return summary;
}

class DataPlaneClient {
public:
    explicit DataPlaneClient(std::shared_ptr<Channel> channel)
        : stub_(DataPlaneGossip::NewStub(channel)) {}

    // ONLY sends logs — no anti-entropy logic here
    bool SyncLogs(const std::string& my_node_id,
                  const std::string& my_address,
                  const std::vector<ChatEntry>& entries) {

        SyncLogsRequest req;
        NodeInfo* sender = req.mutable_sender();
        sender->set_node_id(my_node_id);
        sender->set_address(my_address);

        req.mutable_summary()->CopyFrom(BuildSummary(my_node_id, entries));

        for (const auto& e : entries) {
            ChatMessage* msg = req.add_logs();
            msg->set_message_id(e.message_id);
            msg->set_sender_id(e.sender_id);
            msg->set_payload(e.payload);
            msg->set_lamport_time(e.lamport_time);
            msg->set_epoch(e.epoch);
        }

        SyncLogsResponse resp;
        ClientContext ctx;
        Status status = stub_->SyncLogs(&ctx, req, &resp);

        if (!status.ok()) {
            std::cerr << "SyncLogs failed: " << status.error_message() << "\n";
            return false;
        }

        std::cout << "SyncLogs sent - peer lamport_time="
                  << resp.receiver_lamport_time() << "\n";

        return resp.success();
    }

private:
    std::unique_ptr<DataPlaneGossip::Stub> stub_;
};

static std::vector<ChatEntry> BuildSampleLog(const std::string& node_id) {
    return {
        {node_id + "_1", node_id, "hello world", 1, 0, 0},
        {node_id + "_2", node_id, "second entry", 2, 0, 0},
    };
}

int main(int argc, char** argv) {
    std::string my_node_id = "node-1";
    std::string my_address = "127.0.0.1:50051";

    std::vector<std::string> peers = {"127.0.0.1:50052"};

    if (argc > 1) my_node_id = argv[1];
    if (argc > 2) my_address = argv[2];
    if (argc > 3) {
        peers.clear();
        for (int i = 3; i < argc; ++i) {
            peers.push_back(argv[i]);
        }
    }

    std::cout << "Syncing from " << my_node_id
              << " (" << my_address << ")\n";

    std::vector<ChatEntry> local_log = BuildSampleLog(my_node_id);

    for (const auto& target : peers) {
        std::cout << "  -> peer " << target << "\n";

        // Step 1: summary check
        SyncLogsRequest summary_req;
        NodeInfo* sender = summary_req.mutable_sender();
        sender->set_node_id(my_node_id);
        sender->set_address(my_address);

        summary_req.mutable_summary()->CopyFrom(
            BuildSummary(my_node_id, local_log)
        );

        SyncLogsResponse summary_resp;
        ClientContext ctx;

        auto stub = DataPlaneGossip::NewStub(
            grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));

        Status status = stub->SyncLogs(&ctx, summary_req, &summary_resp);

        if (!status.ok()) {
            std::cerr << "Summary check failed\n";
            continue;
        }

        // Step 2: compare summaries
        auto local = BuildSummary(my_node_id, local_log);

        if (summary_resp.has_summary() &&
            summary_resp.summary().entry_count() == local.entry_count() &&
            summary_resp.summary().max_lamport_time() == local.max_lamport_time()) {

            std::cout << "Peer already up-to-date, skipping log send\n";
            continue;
        }

        // Step 3: send logs only if needed
        DataPlaneClient client(
            grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));

        client.SyncLogs(my_node_id, my_address, local_log);
    }

    return 0;
}