#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include "lamport_clock.h"

TEST_CASE("LamportClock - basic tick", "[clock]") {
    LamportClock clock;

    REQUIRE(clock.get() == 0);
    clock.tick();
    REQUIRE(clock.get() == 1);
    clock.tick();
    REQUIRE(clock.get() == 2);
}

TEST_CASE("LamportClock - tick returns new value", "[clock]") {
    LamportClock clock;
    REQUIRE(clock.tick() == 1);
    REQUIRE(clock.tick() == 2);
    REQUIRE(clock.tick() == 3);
}

TEST_CASE("LamportClock - update when received > local", "[clock]") {
    LamportClock clock;
    clock.tick(); // local = 1

    clock.update(10); // max(1, 10) + 1 = 11
    REQUIRE(clock.get() == 11);
}

TEST_CASE("LamportClock - update when received < local", "[clock]") {
    LamportClock clock;
    for (int i = 0; i < 5; i++) clock.tick(); // local = 5

    clock.update(2); // max(5, 2) + 1 = 6
    REQUIRE(clock.get() == 6);
}

TEST_CASE("LamportClock - update when received == local", "[clock]") {
    LamportClock clock;
    for (int i = 0; i < 5; i++) clock.tick(); // local = 5

    clock.update(5); // max(5, 5) + 1 = 6
    REQUIRE(clock.get() == 6);
}

TEST_CASE("LamportClock - concurrent ticks are all unique", "[clock][concurrency]") {
    LamportClock clock;
    const int N = 1000;

    std::vector<int32_t> results(N);
    std::vector<std::thread> threads;

    for (int i = 0; i < N; i++) {
        threads.emplace_back([&, i]() {
            results[i] = clock.tick();
        });
    }
    for (auto& t : threads) t.join();

    // all tick values must be unique
    std::sort(results.begin(), results.end());
    auto dup = std::adjacent_find(results.begin(), results.end());
    REQUIRE(dup == results.end());
}