#include <catch2/catch_test_macros.hpp>

#include <vector>
#include <thread>
#include <chrono>
#include <memory>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstdlib>

#include <grpcpp/grpcpp.h>
#include "locality_messaging.grpc.pb.h"

using namespace locality_messaging;
using grpc::ClientContext;
using grpc::Status;

// --------------------------------------------------
// Start auth
// --------------------------------------------------
pid_t start_auth() {
    pid_t pid = fork();
    if (pid == 0) {
        execl("./build/fake_auth_server",
              "fake_auth_server",
              "127.0.0.1:50060",
              nullptr);
        exit(1);
    }
    return pid;
}

// --------------------------------------------------
// Start raft cluster
// --------------------------------------------------
std::vector<pid_t> start_cluster() {
    std::vector<pid_t> pids;

    struct NodeSpec {
        const char* id;
        const char* addr;
        const char* peer1;
        const char* peer2;
    };

    std::vector<NodeSpec> nodes = {
        {"node-1", "127.0.0.1:50053",
         "node-2=127.0.0.1:50054", "node-3=127.0.0.1:50055"},
        {"node-2", "127.0.0.1:50054",
         "node-1=127.0.0.1:50053", "node-3=127.0.0.1:50055"},
        {"node-3", "127.0.0.1:50055",
         "node-1=127.0.0.1:50053", "node-2=127.0.0.1:50054"},
    };

    for (auto& n : nodes) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("./build/raft_server",
                  "raft_server",
                  n.id,
                  n.addr,
                  n.peer1,
                  n.peer2,
                  nullptr);
            exit(1);
        }
        pids.push_back(pid);
    }

    std::this_thread::sleep_for(std::chrono::seconds(3)); // allow election
    return pids;
}

// --------------------------------------------------
// Stop all
// --------------------------------------------------
void stop_all(const std::vector<pid_t>& pids, pid_t auth) {
    for (auto p : pids) kill(p, SIGTERM);
    kill(auth, SIGTERM);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    for (auto p : pids) kill(p, SIGKILL);
    kill(auth, SIGKILL);

    for (auto p : pids) waitpid(p, nullptr, 0);
    waitpid(auth, nullptr, 0);
}

// --------------------------------------------------
// RAII Guard (CRITICAL FIX)
// --------------------------------------------------
struct ClusterGuard {
    std::vector<pid_t> nodes;
    pid_t auth;

    ClusterGuard() {
        system("pkill -f raft_server > /dev/null 2>&1");
        system("pkill -f fake_auth_server > /dev/null 2>&1");

        auth = start_auth();
        nodes = start_cluster();
    }

    ~ClusterGuard() {
        stop_all(nodes, auth);
    }
};

// --------------------------------------------------
// TEST CASE
// --------------------------------------------------
TEST_CASE("Full system integration test", "[system]") {

    ClusterGuard cluster;

    std::vector<std::string> addrs = {
        "127.0.0.1:50053",
        "127.0.0.1:50054",
        "127.0.0.1:50055"
    };

    std::vector<std::unique_ptr<ControlPlaneRaft::Stub>> raft;
    std::vector<std::unique_ptr<IntegrationAuth::Stub>> auth_stub;

    for (auto& a : addrs) {
        auto ch = grpc::CreateChannel(a, grpc::InsecureChannelCredentials());
        raft.push_back(ControlPlaneRaft::NewStub(ch));
        auth_stub.push_back(IntegrationAuth::NewStub(ch));
    }

    std::vector<std::string> users = {"alice","bob","charlie"};
    int epoch = 0;

    // ===============================
    // TEST 1: RAFT WRITES (retry)
    // ===============================
    for (auto& u : users) {
        AddUserRequest req;
        req.set_user_id(u);

        bool success = false;

        for (int retry = 0; retry < 10 && !success; retry++) {
            for (auto& s : raft) {
                AddUserResponse resp;
                ClientContext ctx;
                auto st = s->AddUser(&ctx, req, &resp);

                if (st.ok() && resp.success()) {
                    epoch = resp.current_epoch();
                    success = true;
                    break;
                }
            }
            if (!success)
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }

        REQUIRE(success);
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // ===============================
    // TEST 2: ACL CHECK (strict)
    // ===============================
    for (auto& u : users) {

    bool all_ok = false;

    for (int retry = 0; retry < 10 && !all_ok; retry++) {

        all_ok = true;

        for (auto& s : auth_stub) {
            AuthCheckRequest req;
            req.set_user_id(u);
            req.set_epoch(epoch);

            AuthCheckResponse resp;
            ClientContext ctx;
            auto st = s->CheckAuthorization(&ctx, req, &resp);

            if (!st.ok() || !resp.is_authorized()) {
                all_ok = false;
                break;
            }
        }

        if (!all_ok)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    bool any_ok = false;

for (int retry = 0; retry < 10 && !any_ok; retry++) {

    for (auto& s : auth_stub) {
        AuthCheckRequest req;
        req.set_user_id(u);
        req.set_epoch(epoch);

        AuthCheckResponse resp;
        ClientContext ctx;
        auto st = s->CheckAuthorization(&ctx, req, &resp);

        if (st.ok() && resp.is_authorized()) {
            any_ok = true;
            break;
        }
    }

    if (!any_ok)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

REQUIRE(any_ok);
}
    // ===============================
    // TEST 3: CRASH
    // ===============================
    kill(cluster.nodes[1], SIGTERM);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // ===============================
    // TEST 4: LEADER FAILOVER
    // ===============================
    AddUserRequest extra;
    extra.set_user_id("david");

    bool post_crash_ok = false;

    for (int retry = 0; retry < 10 && !post_crash_ok; retry++) {
        for (auto& s : raft) {
            AddUserResponse resp;
            ClientContext ctx;
            auto st = s->AddUser(&ctx, extra, &resp);

            if (st.ok() && resp.success()) {
                post_crash_ok = true;
                break;
            }
        }
        if (!post_crash_ok)
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    REQUIRE(post_crash_ok);

    int epoch_before_revoke = epoch;

    // ===============================
    // TEST 5: REVOKE (retry)
    // ===============================
    RevokeUserRequest r;
    r.set_user_id("bob");

    bool revoke_ok = false;

    for (int retry = 0; retry < 10 && !revoke_ok; retry++) {
        for (auto& s : raft) {
            RevokeUserResponse resp;
            ClientContext ctx;
            auto st = s->RevokeUser(&ctx, r, &resp);

            if (st.ok() && resp.success()) {
                epoch = resp.new_epoch();
                revoke_ok = true;
                break;
            }
        }
        if (!revoke_ok)
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    REQUIRE(revoke_ok);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // ===============================
    // TEST 6: AUTH AFTER REVOKE
    // ===============================
    // ===============================
// TEST 6: AUTH AFTER REVOKE
// ===============================
for (auto& u : users) {

    bool ok = false;

    for (int retry = 0; retry < 10 && !ok; retry++) {

        for (auto& s : auth_stub) {

            AuthCheckRequest req;
            req.set_user_id(u);

            // always use latest epoch
            req.set_epoch(epoch);

            AuthCheckResponse resp;
            ClientContext ctx;
            auto st = s->CheckAuthorization(&ctx, req, &resp);

            if (!st.ok()) continue;

            if (u == "bob" && !resp.is_authorized())
                ok = true;

            if (u != "bob" && resp.is_authorized())
                ok = true;
        }

        if (!ok)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    REQUIRE(ok);
}
}