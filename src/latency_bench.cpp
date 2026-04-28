#include "log_store.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <memory>

void bench(int k, int entries) {
    std::vector<std::unique_ptr<LogStore>> nodes;

    for (int i = 0; i < k; i++)
        nodes.emplace_back(std::make_unique<LogStore>("node-" + std::to_string(i)));

    for (int i = 0; i < entries; i++)
        nodes[0]->append("payload-" + std::to_string(i));

    auto t_start = std::chrono::steady_clock::now();

    for (int i = 1; i < k; i++)
        nodes[i]->merge(nodes[0]->all());

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start).count();

    bool converged = true;

    for (int i = 1; i < k; i++)
        if (nodes[i]->get_hash() != nodes[0]->get_hash())
            converged = false;

    std::cout << "K=" << k
              << " entries=" << entries
              << " time=" << ms << "ms"
              << " converged=" << (converged ? "yes" : "no") << "\n";
}

int main() {
    std::cout << "=== Fanout latency benchmark ===\n";

    for (int entries : {100, 500, 1000})
        for (int k : {2, 3, 5})
            bench(k, entries);

    return 0;
}