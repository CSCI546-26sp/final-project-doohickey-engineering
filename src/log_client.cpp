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
                  << resp.receiver_lamport_time() << "\n";
        return resp.success();
    }

private:
    std::unique_ptr<DataPlaneGossip::Stub> stub_;
};

int main(int argc, char** argv) {
    std::string target = argc > 1 ? argv[1] : "localhost:50052";

    DataPlaneClient client(
        grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));

    std::vector<ChatEntry> local_log = {
        {"node-1_1", "node-1", "hello world",  1, 0, 0},
        {"node-1_2", "node-1", "second entry", 2, 0, 0},
    };

    client.SyncLogs("node-1", "localhost:50051", local_log);
    return 0;
}
