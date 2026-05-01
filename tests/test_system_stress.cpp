
/*
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <memory>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <grpcpp/grpcpp.h>
#include "locality_messaging.grpc.pb.h"

using namespace locality_messaging;
using grpc::ClientContext;
using grpc::Status;

// ---------------------------------------------------------------
void print_separator(const std::string& title = "") {
    std::cout << "\n" << std::string(60, '=') << "\n";
    if (!title.empty()) {
        std::cout << "  " << title << "\n";
        std::cout << std::string(60, '=') << "\n";
    }
}

// ---------------------------------------------------------------
std::vector<pid_t> start_cluster() {
    std::vector<pid_t> pids;

    struct Node {
        std::string id;
        std::string addr;
    };

    std::vector<Node> nodes = {
        {"auth-1", "127.0.0.1:50053"},
        {"auth-2", "127.0.0.1:50054"},
        {"auth-3", "127.0.0.1:50055"}
    };

    for (int i = 0; i < nodes.size(); i++) {
        pid_t pid = fork();

        if (pid == 0) {
            std::vector<std::string> args;
            args.push_back("raft_server");
            args.push_back(nodes[i].id);
            args.push_back(nodes[i].addr);

            for (int j = 0; j < nodes.size(); j++) {
                if (i == j) continue;
                args.push_back(nodes[j].id + "=" + nodes[j].addr);
            }

            std::vector<char*> cargs;
            for (auto& s : args)
                cargs.push_back(const_cast<char*>(s.c_str()));
            cargs.push_back(nullptr);

            execv("./build/raft_server", cargs.data());
            exit(1);
        }

        pids.push_back(pid);
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));
    return pids;
}

// ---------------------------------------------------------------
void stop_cluster(const std::vector<pid_t>& pids) {
    for (pid_t pid : pids) kill(pid, SIGTERM);
    for (pid_t pid : pids) waitpid(pid, nullptr, 0);
}

// ---------------------------------------------------------------
int main() {
    print_separator("RAFT CRASH RECOVERY TEST");

    auto pids = start_cluster();

    std::vector<std::string> addrs = {
        "127.0.0.1:50053",
        "127.0.0.1:50054",
        "127.0.0.1:50055"
    };

    std::vector<std::unique_ptr<ControlPlaneRaft::Stub>> raft;
    std::vector<std::unique_ptr<IntegrationAuth::Stub>> auth;

    for (auto& a : addrs) {
        auto ch = grpc::CreateChannel(a, grpc::InsecureChannelCredentials());
        raft.push_back(ControlPlaneRaft::NewStub(ch));
        auth.push_back(IntegrationAuth::NewStub(ch));
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    int epoch = 0;

    // ===============================
    // TEST 1: Add users
    // ===============================
    print_separator("TEST 1: ADD USERS");

    std::vector<std::string> users = {"alice","bob"};

    for (auto& u : users) {
        AddUserRequest req;
        req.set_user_id(u);

        for (auto& s : raft) {
            AddUserResponse resp;
            ClientContext ctx;
            auto st = s->AddUser(&ctx, req, &resp);

            if (st.ok() && resp.success()) {
                epoch = resp.current_epoch();
                break;
            }
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // ===============================
    // TEST 2: CRASH LEADER
    // ===============================
    print_separator("TEST 2: CRASH NODE");

    kill(pids[0], SIGTERM);
    std::cout << "Killed node auth-1\n";

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // ===============================
    // TEST 3: WRITE AFTER CRASH
    // ===============================
    print_separator("TEST 3: WRITE AFTER CRASH");

    AddUserRequest extra;
    extra.set_user_id("charlie");

    bool success = false;

    for (int retry = 0; retry < 10 && !success; retry++) {
        for (auto& s : raft) {
            AddUserResponse resp;
            ClientContext ctx;
            auto st = s->AddUser(&ctx, extra, &resp);

            if (st.ok() && resp.success()) {
                success = true;
                epoch = resp.current_epoch();
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!success) {
        std::cerr << "❌ Write failed after crash\n";
    } else {
        std::cout << "✓ Write succeeded after crash\n";
    }

    // ===============================
    // TEST 4: VERIFY STATE
    // ===============================
    print_separator("TEST 4: VERIFY STATE");

    for (auto& u : {"alice","charlie"}) {

        bool ok = false;

        for (int retry = 0; retry < 10 && !ok; retry++) {
            for (auto& s : auth) {

                AuthCheckRequest req;
                req.set_user_id(u);
                req.set_epoch(epoch);

                AuthCheckResponse resp;
                ClientContext ctx;

                auto st = s->CheckAuthorization(&ctx, req, &resp);

                if (st.ok() && resp.is_authorized()) {
                    ok = true;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (!ok) {
            std::cerr << "❌ State lost for user: " << u << "\n";
        } else {
            std::cout << "✓ State preserved for user: " << u << "\n";
        }
    }

    // ===============================
    // CLEANUP
    // ===============================
    print_separator("CLEANUP");
    stop_cluster(pids);

    std::cout << "\n✓ CRASH RECOVERY TEST COMPLETE\n";
    return 0;
}
*/
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <memory>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <grpcpp/grpcpp.h>
#include "locality_messaging.grpc.pb.h"

using namespace locality_messaging;
using grpc::ClientContext;

// ---------------------------------------------------------------
std::vector<pid_t> start_cluster() {
    std::vector<pid_t> pids;

    std::vector<std::pair<std::string,std::string>> nodes = {
        {"auth-1","127.0.0.1:50053"},
        {"auth-2","127.0.0.1:50054"},
        {"auth-3","127.0.0.1:50055"}
    };

    for (int i = 0; i < nodes.size(); i++) {
        pid_t pid = fork();

        if (pid == 0) {
            std::vector<std::string> args = {
                "raft_server",
                nodes[i].first,
                nodes[i].second
            };

            for (int j = 0; j < nodes.size(); j++) {
                if (i == j) continue;
                args.push_back(nodes[j].first + "=" + nodes[j].second);
            }

            std::vector<char*> cargs;
            for (auto& s : args) cargs.push_back((char*)s.c_str());
            cargs.push_back(nullptr);

            execv("./build/raft_server", cargs.data());
            exit(1);
        }

        pids.push_back(pid);
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));
    return pids;
}

// ---------------------------------------------------------------
void stop_node(pid_t pid) {
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
}

// ---------------------------------------------------------------
void stop_all(const std::vector<pid_t>& pids) {
    for (auto p : pids) kill(p, SIGTERM);
    for (auto p : pids) waitpid(p, nullptr, 0);
}

// ---------------------------------------------------------------
int main() {

    auto pids = start_cluster();

    std::vector<std::string> addrs = {
        "127.0.0.1:50053",
        "127.0.0.1:50054",
        "127.0.0.1:50055"
    };

    std::vector<std::unique_ptr<ControlPlaneRaft::Stub>> raft;
    std::vector<std::unique_ptr<IntegrationAuth::Stub>> auth;

    for (auto& a : addrs) {
        auto ch = grpc::CreateChannel(a, grpc::InsecureChannelCredentials());
        raft.push_back(ControlPlaneRaft::NewStub(ch));
        auth.push_back(IntegrationAuth::NewStub(ch));
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    int epoch = 0;

    // =====================================================
    // 1. INITIAL WRITE
    // =====================================================
    AddUserRequest req;
    req.set_user_id("alice");

    for (auto& s : raft) {
        AddUserResponse resp;
        ClientContext ctx;
        if (s->AddUser(&ctx, req, &resp).ok() && resp.success()) {
            epoch = resp.current_epoch();
            break;
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // =====================================================
    // 2. CRASH TEST
    // =====================================================
    std::cout << "CRASH: killing node-1\n";
    kill(pids[0], SIGKILL);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    AddUserRequest req2;
    req2.set_user_id("bob");

    bool ok = false;
    for (int r = 0; r < 10 && !ok; r++) {
        for (auto& s : raft) {
            AddUserResponse resp;
            ClientContext ctx;
            if (s->AddUser(&ctx, req2, &resp).ok() && resp.success()) {
                epoch = resp.current_epoch();
                ok = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << (ok ? "✓ Crash recovery OK\n" : " Crash failed\n");

    // =====================================================
    // 3. RESTART TEST (PERSISTENCE)
    // =====================================================
    std::cout << " RESTART: restarting node-1\n";

    pid_t restarted = fork();
    if (restarted == 0) {
        execl("./build/raft_server",
              "raft_server",
              "auth-1",
              "127.0.0.1:50053",
              "auth-2=127.0.0.1:50054",
              "auth-3=127.0.0.1:50055",
              nullptr);
        exit(1);
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // verify state persisted
    bool persisted = false;

    for (auto& s : auth) {
        AuthCheckRequest r;
        r.set_user_id("alice");
        r.set_epoch(epoch);

        AuthCheckResponse resp;
        ClientContext ctx;

        if (s->CheckAuthorization(&ctx, r, &resp).ok() && resp.is_authorized()) {
            persisted = true;
            break;
        }
    }

    std::cout << (persisted ? "✓ Restart persistence OK\n" : " Persistence failed\n");

    // =====================================================
    // 4. PARTITION TEST (simulated)
    // =====================================================
    std::cout << " PARTITION: killing node-2 (simulate partition)\n";

    kill(pids[1], SIGKILL);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    AddUserRequest req3;
    req3.set_user_id("charlie");

    bool partition_ok = false;

    for (int r = 0; r < 10 && !partition_ok; r++) {
        for (auto& s : raft) {
            AddUserResponse resp;
            ClientContext ctx;
            if (s->AddUser(&ctx, req3, &resp).ok() && resp.success()) {
                epoch = resp.current_epoch();
                partition_ok = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << (partition_ok ? "✓ Majority partition OK\n" : " Partition failed\n");

    // =====================================================
    // 5. CONVERGENCE TEST
    // =====================================================
    std::cout << " CONVERGENCE CHECK\n";

    std::vector<bool> results;

    for (auto& s : auth) {
        AuthCheckRequest r;
        r.set_user_id("charlie");
        r.set_epoch(epoch);

        AuthCheckResponse resp;
        ClientContext ctx;

        if (s->CheckAuthorization(&ctx, r, &resp).ok())
            results.push_back(resp.is_authorized());
    }

    bool consistent = true;
    for (int i = 1; i < results.size(); i++)
        if (results[i] != results[0])
            consistent = false;

    std::cout << (consistent ? "✓ Convergence OK\n" : " Convergence failed\n");

    // =====================================================
    // CLEANUP
    // =====================================================
    stop_all(pids);
    kill(restarted, SIGTERM);

    std::cout << "\n FULL SYSTEM STRESS TEST COMPLETE\n";
    return 0;
}