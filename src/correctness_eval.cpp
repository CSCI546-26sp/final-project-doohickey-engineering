#include "log_store.h"
#include <iostream>
#include <thread>
#include <vector>

int main() {
    LogStore n1("node-1"), n2("node-2"), n3("node-3");
    const int N = 200;

    std::cout << "=== Writing independently ===\n";
    std::vector<std::thread> threads;
    for (int i = 0; i < N; i++) {
        threads.emplace_back([&,i](){ n1.append("a"+std::to_string(i)); });
        threads.emplace_back([&,i](){ n2.append("b"+std::to_string(i)); });
        threads.emplace_back([&,i](){ n3.append("c"+std::to_string(i)); });
    }
    for (auto& t : threads) t.join();

    std::cout << "sizes: "
              << n1.size() << " "
              << n2.size() << " "
              << n3.size() << "\n";

    std::cout << "=== Syncing ===\n";
    n1.merge(n2.all()); n1.merge(n3.all());
    n2.merge(n1.all());
    n3.merge(n1.all());

    bool size_ok  = n1.size() == n2.size() && n2.size() == n3.size();
    bool hash_ok  = n1.get_hash() == n2.get_hash() &&
                    n2.get_hash() == n3.get_hash();
    bool order_ok = true;
    auto all = n1.all();
    for (size_t i = 1; i < all.size(); i++)
        if (all[i].lamport_time < all[i-1].lamport_time)
            { order_ok = false; break; }

    std::cout << "size_ok:  " << (size_ok  ? "PASS" : "FAIL") << "\n";
    std::cout << "hash_ok:  " << (hash_ok  ? "PASS" : "FAIL") << "\n";
    std::cout << "order_ok: " << (order_ok ? "PASS" : "FAIL") << "\n";

    return (size_ok && hash_ok && order_ok) ? 0 : 1;
}