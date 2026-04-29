#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <thread>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <grpcpp/grpcpp.h>
#include "locality_messaging.grpc.pb.h"

using namespace locality_messaging;
using grpc::ClientContext;

// ── Config ─────────────────────────────────────────
static const std::vector<std::string> ADDRS = {
    "127.0.0.1:50053",
    "127.0.0.1:50054",
    "127.0.0.1:50055"
};

static constexpr int RUNS = 5;
static constexpr int OPS  = 1000;

// ── Start auth ─────────────────────────────────────
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

// ── Start raft cluster ─────────────────────────────
std::vector<pid_t> start_cluster() {
    std::vector<pid_t> pids;

    struct Node {
        const char* id;
        const char* addr;
        const char* p1;
        const char* p2;
    };

    std::vector<Node> nodes = {
        {"node-1","127.0.0.1:50053","node-2=127.0.0.1:50054","node-3=127.0.0.1:50055"},
        {"node-2","127.0.0.1:50054","node-1=127.0.0.1:50053","node-3=127.0.0.1:50055"},
        {"node-3","127.0.0.1:50055","node-1=127.0.0.1:50053","node-2=127.0.0.1:50054"},
    };

    for (auto& n : nodes) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("./build/raft_server",
                  "raft_server",
                  n.id,
                  n.addr,
                  n.p1,
                  n.p2,
                  nullptr);
            exit(1);
        }
        pids.push_back(pid);
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));
    return pids;
}

// ── Cleanup ────────────────────────────────────────
void stop_all(const std::vector<pid_t>& nodes, pid_t auth) {
    for (auto p : nodes) kill(p, SIGTERM);
    kill(auth, SIGTERM);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    for (auto p : nodes) kill(p, SIGKILL);
    kill(auth, SIGKILL);

    for (auto p : nodes) waitpid(p, nullptr, 0);
    waitpid(auth, nullptr, 0);
}

// ── Find leader ────────────────────────────────────
int find_leader(const std::vector<std::unique_ptr<ControlPlaneRaft::Stub>>& stubs) {
    for (int i = 0; i < (int)stubs.size(); i++) {
        AddUserRequest req;
        req.set_user_id("__probe__");

        AddUserResponse resp;
        ClientContext ctx;

        auto st = stubs[i]->AddUser(&ctx, req, &resp);
        if (st.ok() && resp.success()) {
            std::cout << "[leader] node-" << i+1 << "\n";
            return i;
        }
    }
    return -1;
}

// ── Run one experiment ─────────────────────────────
std::vector<double> run_once(int leader) {
    auto channel = grpc::CreateChannel(ADDRS[leader],
                                       grpc::InsecureChannelCredentials());
    auto stub = ControlPlaneRaft::NewStub(channel);

    std::vector<double> lat;
    lat.reserve(OPS);

    for (int i = 0; i < OPS; i++) {
        AddUserRequest req;
        req.set_user_id("user-" + std::to_string(i));

        AddUserResponse resp;
        ClientContext ctx;

        auto t0 = std::chrono::steady_clock::now();
        stub->AddUser(&ctx, req, &resp);
        auto t1 = std::chrono::steady_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        lat.push_back(ms);
    }

    return lat;
}

// ── Main ───────────────────────────────────────────
int main() {

    system("pkill -f raft_server > /dev/null 2>&1");
    system("pkill -f fake_auth_server > /dev/null 2>&1");

    pid_t auth = start_auth();
    auto nodes = start_cluster();

    std::vector<std::unique_ptr<ControlPlaneRaft::Stub>> stubs;
    for (auto& a : ADDRS) {
        auto ch = grpc::CreateChannel(a, grpc::InsecureChannelCredentials());
        stubs.push_back(ControlPlaneRaft::NewStub(ch));
    }

    int leader = find_leader(stubs);
    if (leader < 0) {
        std::cerr << "No leader found\n";
        stop_all(nodes, auth);
        return 1;
    }

    std::ofstream out("latency_runs.txt");

    std::vector<double> avg_all, min_all, max_all;

    std::cout << "\n=== LATENCY TEST ===\n";

    for (int r = 0; r < RUNS; r++) {

        auto lat = run_once(leader);
        std::sort(lat.begin(), lat.end());

        double avg  = std::accumulate(lat.begin(), lat.end(), 0.0) / lat.size();
        double minv = lat.front();
        double maxv = lat.back();

        avg_all.push_back(avg);
        min_all.push_back(minv);
        max_all.push_back(maxv);

        std::cout << "Run " << r+1
                  << " | avg=" << avg
                  << " ms | min=" << minv
                  << " ms | max=" << maxv << " ms\n";

        out << r+1 << " " << avg << " " << minv << " " << maxv << "\n";
    }

    double final_avg = std::accumulate(avg_all.begin(), avg_all.end(), 0.0) / RUNS;
    double final_min = *std::min_element(min_all.begin(), min_all.end());
    double final_max = *std::max_element(max_all.begin(), max_all.end());

    std::cout << "\n=== FINAL ===\n";
    std::cout << "Avg: " << final_avg
              << " | Min: " << final_min
              << " | Max: " << final_max << "\n";

    out.close();

    stop_all(nodes, auth);

    return 0;
}