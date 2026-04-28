/*
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include "locality_messaging.grpc.pb.h"
#include <unistd.h>
#include <signal.h>

using namespace locality_messaging;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

void print_separator(const std::string& title = "") {
    std::cout << "\n" << std::string(60, '=') << "\n";
    if (!title.empty()) {
        std::cout << "  " << title << "\n";
        std::cout << std::string(60, '=') << "\n";
    }
}

std::vector<pid_t> start_cluster() {
    std::vector<pid_t> pids;

    struct NodeConfig {
        std::string node_id;
        std::string addr;
    };

    std::vector<NodeConfig> nodes = {
        {"node-1", "127.0.0.1:50053"},
        {"node-2", "127.0.0.1:50054"},
        {"node-3", "127.0.0.1:50055"}
    };

    for (const auto& node : nodes) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("./build/raft_server",
                  "raft_server",
                  node.node_id.c_str(),
                  node.addr.c_str(),
                  "localhost:50053",
                  "localhost:50054",
                  "localhost:50055",
                  nullptr);

            perror("execl failed");
            exit(1);
        } else {
            pids.push_back(pid);
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    return pids;
}

void stop_cluster(const std::vector<pid_t>& pids) {
    for (pid_t pid : pids) {
        kill(pid, SIGTERM);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
int main(int argc, char** argv) {
    auto pids = start_cluster();
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <node_addr> [node_addr2 ...]\n";
        std::cerr << "Example: " << argv[0] << " localhost:50053 localhost:50054 localhost:50055\n";
        return 1;
    }

    std::vector<std::string> node_addrs;
    for (int i = 1; i < argc; ++i) {
        node_addrs.push_back(argv[i]);
    }

    print_separator("RAFT CONSENSUS ACL TEST CLIENT");
    std::cout << "Connecting to " << node_addrs.size() << " node(s)...\n";
    for (const auto& addr : node_addrs) {
        std::cout << "  - " << addr << "\n";
    }

    // Create stubs for all nodes
    std::vector<std::unique_ptr<ControlPlaneRaft::Stub>> raft_stubs;
    std::vector<std::unique_ptr<IntegrationAuth::Stub>> auth_stubs;

    for (const auto& addr : node_addrs) {
        auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        raft_stubs.push_back(ControlPlaneRaft::NewStub(channel));
        auth_stubs.push_back(IntegrationAuth::NewStub(channel));
    }

    std::cout << "\nWaiting for nodes to stabilize (2 seconds)...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    bool all_tests_passed = true;

    // ========== TEST 1: Add Users ==========
    print_separator("TEST 1: ADD USERS");

    std::vector<std::string> test_users = {"alice", "bob", "charlie"};
    int32_t initial_epoch = 0;

    for (const auto& user : test_users) {
        std::cout << "\n[TEST] Adding user: " << user << "\n";

        AddUserRequest req;
        req.set_user_id(user);

        for (size_t i = 0; i < raft_stubs.size(); ++i) {
            AddUserResponse resp;
            ClientContext ctx;
            Status status = raft_stubs[i]->AddUser(&ctx, req, &resp);

            std::cout << "  Node " << i << " (" << node_addrs[i] << "): ";
            if (status.ok()) {
                if (resp.success()) {
                    std::cout << "✓ SUCCESS (is_leader=true, epoch=" << resp.current_epoch() << ")\n";
                    if (initial_epoch == 0) {
                        initial_epoch = resp.current_epoch();
                    }
                    break; // Only need to send to leader
                } else {
                    std::cout << "Not leader (epoch=" << resp.current_epoch() << ")\n";
                }
            } else {
                std::cout << "✗ FAILED: " << status.error_message() << "\n";
            }
        }
    }

    std::cout << "\nWaiting for log replication and commit (1 second)...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // ========== TEST 2: GetCertificate ==========
    print_separator("TEST 2: GET CERTIFICATE");

    for (const auto& user : test_users) {
        std::cout << "\n[CERT] Requesting certificate for user: " << user << "\n";

        CertificateRequest req;
        req.set_user_id(user);

        bool certificate_ok = false;
        for (size_t i = 0; i < raft_stubs.size(); ++i) {
            CertificateResponse resp;
            ClientContext ctx;
            Status status = raft_stubs[i]->GetCertificate(&ctx, req, &resp);

            std::cout << "  Node " << i << " (" << node_addrs[i] << "): ";
            if (status.ok()) {
                bool valid_user = (resp.user_id() == user);
                bool valid_epoch = (resp.epoch() >= 1);
                if (valid_user && valid_epoch) {
                    std::cout << "✓ CERT OK (user_id=" << resp.user_id()
                              << ", epoch=" << resp.epoch() << ")\n";
                    certificate_ok = true;
                    break;
                } else {
                    std::cout << "✗ INVALID CERT (user_id=" << resp.user_id()
                              << ", epoch=" << resp.epoch() << ")\n";
                }
            } else {
                std::cout << "✗ RPC FAILED: " << status.error_message() << "\n";
            }
        }

        if (!certificate_ok) {
            all_tests_passed = false;
        }
    }

    // ========== TEST 3: Check Authorization (Before Revoke) ==========
    print_separator("TEST 3: CHECK AUTHORIZATION (Before Revoke)");

    std::cout << "\nChecking if users are authorized with epoch " << initial_epoch << ":\n";

    for (const auto& user : test_users) {
        std::cout << "\n[AUTH] Checking user: " << user << " (epoch=" << initial_epoch << ")\n";

        AuthCheckRequest req;
        req.set_user_id(user);
        req.set_epoch(initial_epoch);

        for (size_t i = 0; i < auth_stubs.size(); ++i) {
            AuthCheckResponse resp;
            ClientContext ctx;
            Status status = auth_stubs[i]->CheckAuthorization(&ctx, req, &resp);

            std::cout << "  Node " << i << " (" << node_addrs[i] << "): ";
            if (status.ok()) {
                std::cout << (resp.is_authorized() ? "✓ AUTHORIZED" : "✗ NOT AUTHORIZED") << "\n";
            } else {
                std::cout << "✗ RPC FAILED: " << status.error_message() << "\n";
            }
        }
    }

    // ========== TEST 4: Revoke User ==========
    print_separator("TEST 4: REVOKE USER");

    std::string revoke_user = "bob";
    std::cout << "\n[TEST] Revoking user: " << revoke_user << "\n";

    RevokeUserRequest revoke_req;
    revoke_req.set_user_id(revoke_user);

    int32_t new_epoch = initial_epoch;
    for (size_t i = 0; i < raft_stubs.size(); ++i) {
        RevokeUserResponse resp;
        ClientContext ctx;
        Status status = raft_stubs[i]->RevokeUser(&ctx, revoke_req, &resp);

        std::cout << "  Node " << i << " (" << node_addrs[i] << "): ";
        if (status.ok()) {
            if (resp.success()) {
                std::cout << "✓ SUCCESS (new_epoch=" << resp.new_epoch() << ")\n";
                new_epoch = resp.new_epoch();
                break; // Only need to send to leader
            } else {
                std::cout << "Not leader (new_epoch=" << resp.new_epoch() << ")\n";
            }
        } else {
            std::cout << "✗ FAILED: " << status.error_message() << "\n";
        }
    }

    std::cout << "\nWaiting for revoke log replication and commit (1 second)...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // ========== TEST 5: Check Authorization (After Revoke with Old Epoch) ==========
    print_separator("TEST 5: CHECK WITH OLD EPOCH (After Revoke)");

    std::cout << "\nChecking users with OLD epoch " << initial_epoch << " (should fail for revoked user):\n";

    for (const auto& user : test_users) {
        std::cout << "\n[AUTH] Checking user: " << user << " (epoch=" << initial_epoch << ")\n";

        AuthCheckRequest req;
        req.set_user_id(user);
        req.set_epoch(initial_epoch);

        for (size_t i = 0; i < auth_stubs.size(); ++i) {
            AuthCheckResponse resp;
            ClientContext ctx;
            Status status = auth_stubs[i]->CheckAuthorization(&ctx, req, &resp);

            std::cout << "  Node " << i << " (" << node_addrs[i] << "): ";
            if (status.ok()) {
                std::string expected = (user == revoke_user) ? "NOT AUTHORIZED" : "AUTHORIZED";
                std::string actual = resp.is_authorized() ? "AUTHORIZED" : "NOT AUTHORIZED";
                std::string check = (actual == expected) ? "✓" : "✗";
                std::cout << check << " " << actual << " (expected: " << expected << ")\n";
            } else {
                std::cout << "✗ RPC FAILED: " << status.error_message() << "\n";
            }
        }
    }

    // ========== TEST 6: Check Authorization (After Revoke with New Epoch) ==========
    print_separator("TEST 6: CHECK WITH NEW EPOCH (After Revoke)");

    std::cout << "\nChecking users with NEW epoch " << new_epoch << ":\n";

    for (const auto& user : test_users) {
        std::cout << "\n[AUTH] Checking user: " << user << " (epoch=" << new_epoch << ")\n";

        AuthCheckRequest req;
        req.set_user_id(user);
        req.set_epoch(new_epoch);

        for (size_t i = 0; i < auth_stubs.size(); ++i) {
            AuthCheckResponse resp;
            ClientContext ctx;
            Status status = auth_stubs[i]->CheckAuthorization(&ctx, req, &resp);

            std::cout << "  Node " << i << " (" << node_addrs[i] << "): ";
            if (status.ok()) {
                std::string expected = (user == revoke_user) ? "NOT AUTHORIZED" : "AUTHORIZED";
                std::string actual = resp.is_authorized() ? "AUTHORIZED" : "NOT AUTHORIZED";
                std::string check = (actual == expected) ? "✓" : "✗";
                std::cout << check << " " << actual << " (expected: " << expected << ")\n";
            } else {
                std::cout << "✗ RPC FAILED: " << status.error_message() << "\n";
            }
        }
    }

    print_separator("TEST COMPLETE");
    stop_cluster(pids);

    if (all_tests_passed) {
        std::cout << "\n✓ All tests completed!\n\n";
        return 0;
    }

    std::cout << "\n✗ One or more tests failed\n\n";
    return 1;
}
*/
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
// Utility
// ---------------------------------------------------------------
void print_separator(const std::string& title = "") {
    std::cout << "\n" << std::string(60, '=') << "\n";
    if (!title.empty()) {
        std::cout << "  " << title << "\n";
        std::cout << std::string(60, '=') << "\n";
    }
}

// ---------------------------------------------------------------
// Start fake auth server
// ---------------------------------------------------------------
pid_t start_auth_server() {
    pid_t pid = fork();
    if (pid == 0) {
        execl("./build/fake_auth_server",
              "fake_auth_server",
              "127.0.0.1:50060",
              nullptr);
        perror("execl auth failed");
        exit(1);
    }
    return pid;
}

// ---------------------------------------------------------------
// Start raft cluster
// ---------------------------------------------------------------
std::vector<pid_t> start_cluster() {
    std::vector<pid_t> pids;

    struct Node {
        std::string id;
        std::string addr;
    };

    std::vector<Node> nodes = {
        {"node-1", "127.0.0.1:50053"},
        {"node-2", "127.0.0.1:50054"},
        {"node-3", "127.0.0.1:50055"}
    };

    for (const auto& node : nodes) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            exit(1);
        }

        if (pid == 0) {
            execl("./build/raft_server",
                  "raft_server",
                  node.id.c_str(),
                  node.addr.c_str(),
                  "127.0.0.1:50060",   // auth server
                  nullptr);

            perror("execl raft failed");
            exit(1);
        }

        pids.push_back(pid);
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    return pids;
}

// ---------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------
void stop_cluster(const std::vector<pid_t>& pids, pid_t auth_pid) {
    for (pid_t pid : pids) {
        kill(pid, SIGTERM);
    }
    kill(auth_pid, SIGTERM);

    for (pid_t pid : pids) {
        waitpid(pid, nullptr, 0);
    }
    waitpid(auth_pid, nullptr, 0);
}

// ---------------------------------------------------------------
// MAIN TEST
// ---------------------------------------------------------------
int main() {
    print_separator("RAFT CONSENSUS ACL TEST CLIENT");

    pid_t auth_pid = start_auth_server();
    auto pids = start_cluster();

    std::vector<std::string> node_addrs = {
        "127.0.0.1:50053",
        "127.0.0.1:50054",
        "127.0.0.1:50055"
    };

    std::vector<std::unique_ptr<ControlPlaneRaft::Stub>> raft_stubs;
    std::vector<std::unique_ptr<IntegrationAuth::Stub>> auth_stubs;

    for (auto& addr : node_addrs) {
        auto ch = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        raft_stubs.push_back(ControlPlaneRaft::NewStub(ch));
        auth_stubs.push_back(IntegrationAuth::NewStub(ch));
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    bool all_ok = true;
    int32_t epoch = 0;

    std::vector<std::string> users = {"alice", "bob", "charlie"};

    // ===============================================================
    // TEST 1: Add Users
    // ===============================================================
    print_separator("TEST 1: ADD USERS");

    for (auto& user : users) {
        AddUserRequest req;
        req.set_user_id(user);

        for (auto& stub : raft_stubs) {
            AddUserResponse resp;
            ClientContext ctx;
            auto status = stub->AddUser(&ctx, req, &resp);

            if (status.ok() && resp.success()) {
                std::cout << "Added user " << user << "\n";
                epoch = resp.current_epoch();
                break;
            }
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // ===============================================================
    // TEST 2: Get Certificate
    // ===============================================================
    print_separator("TEST 2: GET CERTIFICATE");

    for (auto& user : users) {
        CertificateRequest req;
        req.set_user_id(user);

        for (auto& stub : raft_stubs) {
            CertificateResponse resp;
            ClientContext ctx;
            auto status = stub->GetCertificate(&ctx, req, &resp);

            if (status.ok() && resp.user_id() == user) {
                std::cout << "Certificate OK for " << user << "\n";
                break;
            }
        }
    }

    // ===============================================================
    // TEST 3: Auth before revoke
    // ===============================================================
    print_separator("TEST 3: AUTH BEFORE REVOKE");

    for (auto& user : users) {
        AuthCheckRequest req;
        req.set_user_id(user);
        req.set_epoch(epoch);

        for (auto& stub : auth_stubs) {
            AuthCheckResponse resp;
            ClientContext ctx;
            stub->CheckAuthorization(&ctx, req, &resp);

            std::cout << user << ": "
                      << (resp.is_authorized() ? "AUTHORIZED" : "DENIED")
                      << "\n";
        }
    }

    // ===============================================================
    // TEST 4: Revoke user
    // ===============================================================
    print_separator("TEST 4: REVOKE USER");

    std::string revoked = "bob";

    RevokeUserRequest rreq;
    rreq.set_user_id(revoked);

    for (auto& stub : raft_stubs) {
        RevokeUserResponse resp;
        ClientContext ctx;
        auto status = stub->RevokeUser(&ctx, rreq, &resp);

        if (status.ok() && resp.success()) {
            epoch = resp.new_epoch();
            std::cout << "Revoked " << revoked << "\n";
            break;
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // ===============================================================
    // TEST 5: Old epoch check
    // ===============================================================
    print_separator("TEST 5: OLD EPOCH CHECK");

    for (auto& user : users) {
        AuthCheckRequest req;
        req.set_user_id(user);
        req.set_epoch(epoch - 1);

        for (auto& stub : auth_stubs) {
            AuthCheckResponse resp;
            ClientContext ctx;
            stub->CheckAuthorization(&ctx, req, &resp);

            std::cout << user << " old epoch: "
                      << (resp.is_authorized() ? "AUTHORIZED" : "DENIED")
                      << "\n";
        }
    }

    // ===============================================================
    // TEST 6: New epoch check
    // ===============================================================
    print_separator("TEST 6: NEW EPOCH CHECK");

    for (auto& user : users) {
        AuthCheckRequest req;
        req.set_user_id(user);
        req.set_epoch(epoch);

        for (auto& stub : auth_stubs) {
            AuthCheckResponse resp;
            ClientContext ctx;
            stub->CheckAuthorization(&ctx, req, &resp);

            std::cout << user << " new epoch: "
                      << (resp.is_authorized() ? "AUTHORIZED" : "DENIED")
                      << "\n";
        }
    }

    // ===============================================================
    // Cleanup
    // ===============================================================
    print_separator("CLEANUP");
    stop_cluster(pids, auth_pid);

    std::cout << "\n✓ TEST COMPLETE\n";
    return 0;
}