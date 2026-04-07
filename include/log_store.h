#pragma once
#include "log_entry.h"
#include "lamport_clock.h"
#include <vector>
#include <optional>
#include <mutex>
#include <string>

class LogStore {
public:
    explicit LogStore(const std::string& node_id, int32_t epoch = 0);

    // local write â€” called by your own client
    ChatEntry append(const std::string& payload);

    // merge incoming entries from a peer (called inside SyncLogs handler)
    // returns false if any entry fails auth check
    void merge(const std::vector<ChatEntry>& incoming);

    std::optional<ChatEntry> read(const std::string& message_id) const;
    std::vector<ChatEntry>   read_range(size_t from, size_t to) const;
    std::vector<ChatEntry>   all() const;
    uint32_t                get_hash() const;

    void set_epoch(int32_t epoch);  // called when Venkat bumps the epoch

private:
    std::string              node_id_;
    int32_t                  current_epoch_;
    std::vector<ChatEntry>    entries_;
    LamportClock             clock_;
    mutable std::mutex       mutex_;

    uint32_t computeChecksum(const ChatEntry& e) const;
};
