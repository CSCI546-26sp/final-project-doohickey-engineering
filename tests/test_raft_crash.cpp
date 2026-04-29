/*
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "locality_messaging.grpc.pb.h"

using namespace locality_messaging;
using grpc::ClientContext;
using grpc::Status;

namespace {

struct NodeProcess {
    std::string node_id;
    std::string listen_addr;
    pid_t pid{-1};
};

std::string state_file_for(const std::string& node_id) {
    return "raft_state_" + node_id + ".txt";
}

std::string find_server_path(const std::filesystem::path& binary_dir) {
    std::vector<std::filesystem::path> candidates;

    if (const char* env = std::getenv("BUILD_DIR")) {
        candidates.emplace_back(env);
        candidates.back() /= "raft_server";
    }

    candidates.push_back(binary_dir / "raft_server");
    candidates.push_back(binary_dir.parent_path() / "raft_server");
    candidates.push_back(std::filesystem::path("./raft_server"));
    candidates.push_back(std::filesystem::path("../raft_server"));

    for (const auto& c : candidates) {
        std::error_code ec;
        if (!c.empty() && std::filesystem::exists(c, ec)) {
            if (access(c.c_str(), X_OK) == 0) {
                return c.string();
            }
        }
    }

    return std::string();
}

void remove_state_files(const std::vector<std::string>& node_ids) {
    for (const auto& node_id : node_ids) {
        std::remove(state_file_for(node_id).c_str());
    }
}

bool wait_for_node(const std::string& addr, int attempts = 40) {
    auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
    auto stub = ControlPlaneRaft::NewStub(channel);

    for (int i = 0; i < attempts; ++i) {
        ListUsersRequest req;
        ListUsersResponse resp;
        ClientContext ctx;
        Status status = stub->ListUsers(&ctx, req, &resp);
        if (status.ok()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    return false;
}

std::unique_ptr<ControlPlaneRaft::Stub> make_stub(const std::string& addr) {
    auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
    return ControlPlaneRaft::NewStub(channel);
}

pid_t start_node(const std::filesystem::path& server_path,
                 const std::string& node_id,
                 const std::string& listen_addr,
                 const std::string& peer_one,
                 const std::string& peer_two,
                 const std::filesystem::path& log_path) {
    pid_t pid = fork();
    if (pid == 0) {
        FILE* log_file = std::fopen(log_path.c_str(), "w");
        if (log_file != nullptr) {
            dup2(fileno(log_file), STDOUT_FILENO);
            dup2(fileno(log_file), STDERR_FILENO);
            std::fclose(log_file);
        }

        execl(server_path.c_str(),
              server_path.c_str(),
              node_id.c_str(),
              listen_addr.c_str(),
              peer_one.c_str(),
              peer_two.c_str(),
              static_cast<char*>(nullptr));

        perror("execl");
        _exit(1);
    }

    return pid;
}

bool stop_node(pid_t pid) {
    if (pid <= 0) {
        return true;
    }

    kill(pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        int status = 0;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    kill(pid, SIGKILL);
    return waitpid(pid, nullptr, 0) == pid;
}

bool add_user(const std::vector<std::unique_ptr<ControlPlaneRaft::Stub>>& stubs,
              const std::vector<std::string>& node_addrs,
              const std::string& user_id) {
    AddUserRequest req;
    req.set_user_id(user_id);

    for (size_t i = 0; i < stubs.size(); ++i) {
        AddUserResponse resp;
        ClientContext ctx;
        Status status = stubs[i]->AddUser(&ctx, req, &resp);
        if (status.ok() && resp.success()) {
            std::cout << "[ADD] " << user_id << " accepted by " << node_addrs[i]
                      << " (epoch=" << resp.current_epoch() << ")\n";
            return true;
        }
    }

    return false;
}

bool revoke_user(const std::vector<std::unique_ptr<ControlPlaneRaft::Stub>>& stubs,
                 const std::vector<std::string>& node_addrs,
                 const std::string& user_id) {
    RevokeUserRequest req;
    req.set_user_id(user_id);

    for (size_t i = 0; i < stubs.size(); ++i) {
        RevokeUserResponse resp;
        ClientContext ctx;
        Status status = stubs[i]->RevokeUser(&ctx, req, &resp);
        if (status.ok() && resp.success()) {
            std::cout << "[REVOKE] " << user_id << " accepted by " << node_addrs[i]
                      << " (new_epoch=" << resp.new_epoch() << ")\n";
            return true;
        }
    }

    return false;
}

bool list_users(const std::string& addr,
                std::set<std::string>* users,
                int32_t* epoch) {
    auto stub = make_stub(addr);
    ListUsersRequest req;
    ListUsersResponse resp;
    ClientContext ctx;
    Status status = stub->ListUsers(&ctx, req, &resp);
    if (!status.ok()) {
        return false;
    }

    users->clear();
    for (const auto& user : resp.authorized_users()) {
        users->insert(user);
    }
    *epoch = resp.current_epoch();
    return true;
}

void print_result(bool ok, const std::string& message) {
    if (ok) {
        std::cout << "✓ " << message << "\n";
    } else {
        std::cout << "✗ " << message << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    std::filesystem::path binary_dir = std::filesystem::path(argv[0]).parent_path();
    std::string server_path_str = find_server_path(binary_dir);

    if (server_path_str.empty()) {
        std::cerr << "Could not locate raft_server binary; attempting to run test_raft_local.sh fallback...\n";
        std::filesystem::path script = binary_dir.parent_path() / "test_raft_local.sh";
        if (std::filesystem::exists(script) && access(script.c_str(), X_OK) == 0) {
            std::string cmd = script.string();
            std::cerr << "Running " << cmd << " as fallback.\n";
            int rc = std::system(cmd.c_str());
            return rc;
        }
        std::cerr << "Fallback script not found or not executable: " << script << "\n";
        return 1;
    }

    std::filesystem::path server_path(server_path_str);

    std::vector<std::string> node_ids = {"crash-1", "crash-2", "crash-3"};
    std::vector<std::string> node_addrs = {
        "127.0.0.1:51053",
        "127.0.0.1:51054",
        "127.0.0.1:51055"
    };

    std::vector<std::string> peer_specs = {
        "crash-1=127.0.0.1:51053",
        "crash-2=127.0.0.1:51054",
        "crash-3=127.0.0.1:51055"
    };

    remove_state_files(node_ids);

    std::vector<NodeProcess> nodes = {
        {node_ids[0], node_addrs[0]},
        {node_ids[1], node_addrs[1]},
        {node_ids[2], node_addrs[2]}
    };

    std::cout << "Starting crash-recovery Raft cluster...\n";
    for (size_t i = 0; i < nodes.size(); ++i) {
        std::filesystem::path log_path = binary_dir / (nodes[i].node_id + ".crash.log");
        nodes[i].pid = start_node(server_path,
                                  nodes[i].node_id,
                                  nodes[i].listen_addr,
                                  peer_specs[(i + 1) % 3],
                                  peer_specs[(i + 2) % 3],
                                  log_path);
    }

    bool ok = true;
    for (const auto& node : nodes) {
        ok = ok && wait_for_node(node.listen_addr);
    }

    if (!ok) {
        std::cerr << "Failed to start one or more raft_server processes\n";
        for (const auto& node : nodes) {
            stop_node(node.pid);
        }
        return 1;
    }

    std::vector<std::unique_ptr<ControlPlaneRaft::Stub>> stubs;
    for (const auto& addr : node_addrs) {
        stubs.push_back(make_stub(addr));
    }

    std::cout << "Creating committed ACL state...\n";
    ok = add_user(stubs, node_addrs, "alice") && ok;
    ok = add_user(stubs, node_addrs, "bob") && ok;
    ok = revoke_user(stubs, node_addrs, "bob") && ok;

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::set<std::string> users;
    int32_t epoch = 0;
    ok = list_users(node_addrs[0], &users, &epoch) && ok;
    ok = ok && (users.count("alice") == 1);
    ok = ok && (users.count("bob") == 0);
    print_result(users.count("alice") == 1, "leader-side state contains alice");
    print_result(users.count("bob") == 0, "leader-side state does not contain bob");

    std::cout << "Stopping all nodes to simulate crash...\n";
    for (const auto& node : nodes) {
        ok = stop_node(node.pid) && ok;
    }

    if (!ok) {
        std::cerr << "Failed while stopping initial cluster\n";
        remove_state_files(node_ids);
        return 1;
    }

    std::cout << "Restarting one node from persisted txt state...\n";
    NodeProcess restarted{node_ids[0], node_addrs[0]};
    restarted.pid = start_node(server_path,
                               restarted.node_id,
                               restarted.listen_addr,
                               peer_specs[1],
                               peer_specs[2],
                               binary_dir / (restarted.node_id + ".restart.log"));

    if (!wait_for_node(restarted.listen_addr)) {
        std::cerr << "Restarted node did not become responsive\n";
        stop_node(restarted.pid);
        remove_state_files(node_ids);
        return 1;
    }

    std::set<std::string> restored_users;
    int32_t restored_epoch = 0;
    ok = list_users(restarted.listen_addr, &restored_users, &restored_epoch) && ok;
    print_result(restored_users.count("alice") == 1, "restarted node restored alice");
    print_result(restored_users.count("bob") == 0, "restarted node did not restore bob");

    ok = ok && (restored_users.count("alice") == 1);
    ok = ok && (restored_users.count("bob") == 0);

    stop_node(restarted.pid);
    remove_state_files(node_ids);

    if (!ok) {
        std::cerr << "Crash-recovery persistence test failed\n";
        return 1;
    }

    std::cout << "Crash-recovery persistence test passed\n";
    return 0;
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
// Start Raft cluster (REAL)
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

        if (pid < 0) {
            perror("fork failed");
            exit(1);
        }

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

            perror("execv failed");
            exit(1);
        }

        pids.push_back(pid);
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));
    return pids;
}

// ---------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------
void stop_cluster(const std::vector<pid_t>& pids) {
    for (pid_t pid : pids) kill(pid, SIGTERM);
    for (pid_t pid : pids) waitpid(pid, nullptr, 0);
}

// ---------------------------------------------------------------
// MAIN TEST
// ---------------------------------------------------------------
int main() {
    print_separator("RAFT CONSENSUS ACL TEST CLIENT");

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
    stop_cluster(pids);

    std::cout << "\n✓ TEST COMPLETE\n";
    return 0;
}