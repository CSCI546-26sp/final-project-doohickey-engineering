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

class DataPlaneClient {
public:
    explicit DataPlaneClient(std::shared_ptr<Channel> channel)
        : stub_(DataPlaneGossip::NewStub(channel)) {}

    bool SyncLogs(const std::string& my_node_id,
                  const std::string& my_address,
                  const std::vector<ChatEntry>& entries) {
        SyncLogsRequest req;
        NodeInfo* sender = req.mutable_sender();
        sender->set_node_id(my_node_id);
        sender->set_address(my_address);

        // Copy local log entries into the request.
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

        std::cout << "SyncLogs ok - peer lamport_time="
                  << resp.receiver_lamport_time()
                  << ", success=" << std::boolalpha << resp.success()
                  << "\n";
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

    // Default to one peer if none are provided.
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
              << " (" << my_address << ")" << "\n";

    std::vector<ChatEntry> local_log = BuildSampleLog(my_node_id);

    // Send the same log to each peer.
    for (const auto& target : peers) {
        std::cout << "  -> peer " << target << "\n";
        DataPlaneClient client(
            grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
        client.SyncLogs(my_node_id, my_address, local_log);
    }

    return 0;
}