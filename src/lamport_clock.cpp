#include "lamport_clock.h"
#include <algorithm>

uint64_t LamportClock::tick() {
    return ++time_;
}

uint64_t LamportClock::get() const {
    return time_.load();
}

void LamportClock::update(uint64_t received) {
    uint64_t current = time_.load();
    uint64_t desired = std::max(current, received) + 1;

    while (!time_.compare_exchange_weak(current, desired)) {
        desired = std::max(current, received) + 1;
    }

}
