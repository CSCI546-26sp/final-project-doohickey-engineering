#include "log_store.h"
#include <iostream>
#include <thread>
#include <chrono>

void simulate_partition() {
    LogStore node1("node-1");
    LogStore node2("node-2");
    LogStore node3("node-3");

    const int N = 100;
    std::cout << "=== Partition started ===\n";

    auto write = [&](LogStore& store, const std::string& prefix) {
        for (int i = 0; i < N; i++) {
            store.append(prefix + "-msg-" + std::to_string(i));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    std::thread t1(write, std::ref(node1), "n1");
    std::thread t2(write, std::ref(node2), "n2");
    std::thread t3(write, std::ref(node3), "n3");
    t1.join(); t2.join(); t3.join();

    std::cout << "sizes before merge: "
              << node1.size() << " "
              << node2.size() << " "
              << node3.size() << "\n";
    std::cout << "hashes before merge: "
              << node1.get_hash() << " "
              << node2.get_hash() << " "
              << node3.get_hash() << "\n";

    std::cout << "\n=== Partition healed ===\n";
    auto t_start = std::chrono::steady_clock::now();

    node1.merge(node2.all());
    node1.merge(node3.all());
    node2.merge(node1.all());
    node3.merge(node1.all());

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start).count();

    std::cout << "sizes after merge: "
              << node1.size() << " "
              << node2.size() << " "
              << node3.size() << "\n";
    std::cout << "hashes after merge: "
              << node1.get_hash() << " "
              << node2.get_hash() << " "
              << node3.get_hash() << "\n";
    std::cout << "convergence time: " << ms << "ms\n";
    std::cout << (node1.get_hash() == node2.get_hash() &&
                  node2.get_hash() == node3.get_hash()
                  ? "CONVERGED ✓" : "DIVERGED ✗") << "\n";
}

int main() {
    simulate_partition();
    return 0;
}