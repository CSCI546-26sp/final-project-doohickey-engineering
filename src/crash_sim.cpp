#include "log_store.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

std::atomic<bool> alive{true};

void write_until_crash(LogStore& store, int n) {
    for (int i = 0; i < n; i++) {
        if (!alive) {
            std::cout << "crashed at write " << i << "\n";
            return;
        }
        store.append("entry-" + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

int main() {
    LogStore node("node-1");
    LogStore peer("node-2");

    for (int i = 0; i < 50; i++)
        peer.append("peer-entry-" + std::to_string(i));

    std::thread writer(write_until_crash, std::ref(node), 100);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    alive = false;
    writer.join();

    std::cout << "entries before resync: " << node.size() << "\n";

    auto t_start = std::chrono::steady_clock::now();
    node.merge(peer.all());
    peer.merge(node.all());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start).count();

    std::cout << "entries after resync: " << node.size() << "\n";
    std::cout << "recovery time: " << ms << "ms\n";
    std::cout << (node.get_hash() == peer.get_hash()
                  ? "CONVERGED ✓" : "DIVERGED ✗") << "\n";
    return 0;
}