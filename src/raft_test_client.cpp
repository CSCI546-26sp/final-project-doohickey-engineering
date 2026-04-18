#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include "locality_messaging.grpc.pb.h"

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

int main(int argc, char** argv) {
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

    // ========== TEST 2: Check Authorization (Before Revoke) ==========
    print_separator("TEST 2: CHECK AUTHORIZATION (Before Revoke)");

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

    // ========== TEST 3: Revoke User ==========
    print_separator("TEST 3: REVOKE USER");

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

    // ========== TEST 4: Check Authorization (After Revoke with Old Epoch) ==========
    print_separator("TEST 4: CHECK WITH OLD EPOCH (After Revoke)");

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

    // ========== TEST 5: Check Authorization (After Revoke with New Epoch) ==========
    print_separator("TEST 5: CHECK WITH NEW EPOCH (After Revoke)");

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
    std::cout << "\n✓ All tests completed!\n\n";

    return 0;
}
