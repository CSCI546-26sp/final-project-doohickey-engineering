#include <catch2/catch_test_macros.hpp>
#include <grpcpp/grpcpp.h>
#include <grpcpp/test/mock_stream.h>
#include <thread>
#include <memory>

#include "locality_messaging.grpc.pb.h"
#include "log_store.h"

using namespace locality_messaging;

// ---------------------------------------------------------------
// Minimal stub auth server — always approves
// ---------------------------------------------------------------
class FakeAuthService final : public IntegrationAuth::Service {
public:
    explicit FakeAuthService(bool always_allow = true)
        : always_allow_(always_allow) {}

    grpc::Status CheckAuthorization(grpc::ServerContext*,
                                    const AuthCheckRequest*,
                                    AuthCheckResponse* resp) override {
        resp->set_is_authorized(always_allow_);
        return grpc::Status::OK;
    }
private:
    bool always_allow_;
};

// ---------------------------------------------------------------
// Test fixture — boots server + auth stub inline
// ---------------------------------------------------------------
struct GrpcFixture {
    std::unique_ptr<grpc::Server> auth_server;
    std::unique_ptr<grpc::Server> data_server;
    std::shared_ptr<grpc::Channel> channel;

    explicit GrpcFixture(bool auth_allow = true) {
        // start fake auth on a random port
        FakeAuthService auth_svc(auth_allow);
        grpc::ServerBuilder auth_builder;
        int auth_port = 0;
        auth_builder.AddListeningPort("127.0.0.1:0",
            grpc::InsecureServerCredentials(), &auth_port);
        auth_builder.RegisterService(&auth_svc);
        auth_server = auth_builder.BuildAndStart();

        std::string auth_addr = "127.0.0.1:" + std::to_string(auth_port);

        // start data plane server on a random port
        // (re-use your DataPlaneGossipImpl — include the header)
        grpc::ServerBuilder data_builder;
        int data_port = 0;
        data_builder.AddListeningPort("127.0.0.1:0",
            grpc::InsecureServerCredentials(), &data_port);
        // DataPlaneGossipImpl needs to be accessible — forward declare or include
        // data_builder.RegisterService(&data_svc);
        data_server = data_builder.BuildAndStart();

        channel = grpc::CreateChannel(
            "127.0.0.1:" + std::to_string(data_port),
            grpc::InsecureChannelCredentials());
    }

    ~GrpcFixture() {
        auth_server->Shutdown();
        data_server->Shutdown();
    }
};

// ---------------------------------------------------------------
// SyncLogs — happy path
// ---------------------------------------------------------------
TEST_CASE("SyncLogs - valid entries accepted", "[grpc]") {
    GrpcFixture fix(/*auth_allow=*/true);
    auto stub = DataPlaneGossip::NewStub(fix.channel);

    SyncLogsRequest req;
    NodeInfo* sender = req.mutable_sender();
    sender->set_node_id("node-2");
    sender->set_address("127.0.0.1:50052");

    ChatMessage* msg = req.add_logs();
    msg->set_message_id("node-2_1");
    msg->set_sender_id("node-2");
    msg->set_payload("hello");
    msg->set_lamport_time(1);
    msg->set_epoch(0);

    SyncLogsResponse resp;
    grpc::ClientContext ctx;
    auto status = stub->SyncLogs(&ctx, req, &resp);

    REQUIRE(status.ok());
    REQUIRE(resp.success());
}

TEST_CASE("SyncLogs - response includes receiver lamport time", "[grpc]") {
    GrpcFixture fix;
    auto stub = DataPlaneGossip::NewStub(fix.channel);

    SyncLogsRequest req;
    req.mutable_sender()->set_node_id("node-2");

    ChatMessage* msg = req.add_logs();
    msg->set_message_id("node-2_7");
    msg->set_sender_id("node-2");
    msg->set_payload("test");
    msg->set_lamport_time(7);
    msg->set_epoch(0);

    SyncLogsResponse resp;
    grpc::ClientContext ctx;
    stub->SyncLogs(&ctx, req, &resp);

    // receiver's clock must advance past the received lamport_time
    REQUIRE(resp.receiver_lamport_time() > 7);
}

// ---------------------------------------------------------------
// SyncLogs — auth rejection
// ---------------------------------------------------------------
TEST_CASE("SyncLogs - unauthorized entries are silently dropped", "[grpc]") {
    GrpcFixture fix(/*auth_allow=*/false); // auth rejects everything
    auto stub = DataPlaneGossip::NewStub(fix.channel);

    SyncLogsRequest req;
    req.mutable_sender()->set_node_id("node-evil");

    ChatMessage* msg = req.add_logs();
    msg->set_message_id("node-evil_1");
    msg->set_sender_id("node-evil");
    msg->set_payload("inject me");
    msg->set_lamport_time(1);
    msg->set_epoch(0);

    SyncLogsResponse resp;
    grpc::ClientContext ctx;
    auto status = stub->SyncLogs(&ctx, req, &resp);

    // RPC itself succeeds (we don't hard-fail the whole call)
    REQUIRE(status.ok());
    // but the entry must not be stored — verify via a ReadRange if you expose it
    // or check store size via a test-only getter
}

// ---------------------------------------------------------------
// SyncLogs — batch with mixed auth
// ---------------------------------------------------------------
TEST_CASE("SyncLogs - mixed batch: valid entries stored, in