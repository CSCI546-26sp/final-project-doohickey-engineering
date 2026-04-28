#include <catch2/catch_test_macros.hpp>
#include <grpcpp/grpcpp.h>
#include <thread>
#include <memory>

#include "locality_messaging.grpc.pb.h"
#include "log_store.h"
#include "data_plane_service.h"   // must contain DataPlaneGossipImpl

using namespace locality_messaging;

// ---------------------------------------------------------------
// Fake Auth Service (always allow / deny)
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
// Test Fixture (starts real gRPC servers)
// ---------------------------------------------------------------
struct GrpcFixture {
    std::unique_ptr<grpc::Server> auth_server;
    std::unique_ptr<grpc::Server> data_server;

    std::unique_ptr<FakeAuthService> auth_svc;
    std::unique_ptr<DataPlaneGossipImpl> data_svc;

    std::shared_ptr<grpc::Channel> channel;

    explicit GrpcFixture(bool auth_allow = true) {
        // ---- Auth server ----
        auth_svc = std::make_unique<FakeAuthService>(auth_allow);

        grpc::ServerBuilder auth_builder;
        int auth_port = 0;
        auth_builder.AddListeningPort("127.0.0.1:0",
            grpc::InsecureServerCredentials(), &auth_port);
        auth_builder.RegisterService(auth_svc.get());

        auth_server = auth_builder.BuildAndStart();
        std::string auth_addr = "127.0.0.1:" + std::to_string(auth_port);

        // ---- Data server ----
        data_svc = std::make_unique<DataPlaneGossipImpl>("node-1", auth_addr);

        grpc::ServerBuilder data_builder;
        int data_port = 0;
        data_builder.AddListeningPort("127.0.0.1:0",
            grpc::InsecureServerCredentials(), &data_port);
        data_builder.RegisterService(data_svc.get());

        data_server = data_builder.BuildAndStart();

        // give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        channel = grpc::CreateChannel(
            "127.0.0.1:" + std::to_string(data_port),
            grpc::InsecureChannelCredentials());
    }

    ~GrpcFixture() {
        data_server->Shutdown();
        auth_server->Shutdown();
    }
};

// ---------------------------------------------------------------
// TESTS
// ---------------------------------------------------------------

TEST_CASE("SyncLogs - valid entries accepted", "[grpc]") {
    GrpcFixture fix(true);
    auto stub = DataPlaneGossip::NewStub(fix.channel);

    SyncLogsRequest req;
    auto* sender = req.mutable_sender();
    sender->set_node_id("node-2");
    sender->set_address("127.0.0.1:50052");

    auto* msg = req.add_logs();
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
    GrpcFixture fix(true);
    auto stub = DataPlaneGossip::NewStub(fix.channel);

    SyncLogsRequest req;
    req.mutable_sender()->set_node_id("node-2");

    auto* msg = req.add_logs();
    msg->set_message_id("node-2_7");
    msg->set_sender_id("node-2");
    msg->set_payload("test");
    msg->set_lamport_time(7);
    msg->set_epoch(0);

    SyncLogsResponse resp;
    grpc::ClientContext ctx;

    stub->SyncLogs(&ctx, req, &resp);

    REQUIRE(resp.receiver_lamport_time() > 7);
}

TEST_CASE("SyncLogs - unauthorized entries are dropped", "[grpc]") {
    GrpcFixture fix(false);  // deny all
    auto stub = DataPlaneGossip::NewStub(fix.channel);

    SyncLogsRequest req;
    req.mutable_sender()->set_node_id("node-evil");

    auto* msg = req.add_logs();
    msg->set_message_id("node-evil_1");
    msg->set_sender_id("node-evil");
    msg->set_payload("inject me");
    msg->set_lamport_time(1);
    msg->set_epoch(0);

    SyncLogsResponse resp;
    grpc::ClientContext ctx;

    auto status = stub->SyncLogs(&ctx, req, &resp);

    REQUIRE(status.ok());
    REQUIRE(resp.success());
}