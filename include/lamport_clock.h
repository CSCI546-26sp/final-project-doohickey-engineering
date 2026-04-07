#pragma once
#include <cstdint>
#include <atomic>

class LamportClock {
public:
    LamportClock() : time_(0) {}
    uint64_t tick();
    uint64_t get() const;
    void update(uint64_t received);
private:
    std::atomic<uint64_t> time_;
};
