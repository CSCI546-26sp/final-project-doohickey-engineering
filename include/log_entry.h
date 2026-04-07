#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ChatEntry {
    std::string message_id;
    std::string sender_id;
    std::string payload;
    int32_t     lamport_time;
    int32_t     epoch;
    uint32_t    checksum;
};
