#include "log_store.h"
#include <functional>
#include <algorithm>
#include <stdexcept>

LogStore::LogStore(const std::string& node_id, int32_t epoch)
    : node_id_(node_id), current_epoch_(epoch) {}

// ---------------------------------------------------------------
// Append
// ---------------------------------------------------------------
ChatEntry LogStore::append(const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);

    ChatEntry e;
    e.lamport_time = static_cast<int32_t>(clock_.tick());
    e.sender_id    = node_id_;
    e.message_id   = node_id_ + "_" + std::to_string(e.lamport_time);
    e.payload      = payload;
    e.epoch        = current_epoch_;
    e.checksum     = computeChecksum(e);

    entries_.push_back(e);
    return e;
}

// ---------------------------------------------------------------
// Read single
// ---------------------------------------------------------------
std::optional<ChatEntry> LogStore::read(const std::string& message_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& e : entries_) {
        if (e.message_id == message_id) return e;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------
// Read range â€” by index (0-based)
// ---------------------------------------------------------------
std::vector<ChatEntry> LogStore::read_range(size_t from, size_t to) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (entries_.empty() || from >= entries_.size()) return {};

    // clamp to actual size
    size_t end = std::min(to, entries_.size() - 1);

    return std::vector<ChatEntry>(
        entries_.begin() + from,
        entries_.begin() + end + 1
    );
}

// ---------------------------------------------------------------
// All entries (used by merge + tests)
// ---------------------------------------------------------------
std::vector<ChatEntry> LogStore::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
}

// ---------------------------------------------------------------
// Hash using standard C++ hash combination
// ---------------------------------------------------------------
uint32_t LogStore::get_hash() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::hash<std::string> hasher;
    uint32_t combined_hash = 0;
    for (const auto& e : entries_) {
        // Standard C++ hash combination
        combined_hash ^= static_cast<uint32_t>(hasher(e.payload)) + 0x9e3779b9 + (combined_hash << 6) + (combined_hash >> 2);
    }
    return combined_hash;
}

// ---------------------------------------------------------------
// Merge â€” CRDT set union from a peer
// ---------------------------------------------------------------
void LogStore::merge(const std::vector<ChatEntry>& incoming) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& remote : incoming) {
        // dedup by message_id
        bool exists = false;
        for (const auto& local : entries_) {
            if (local.message_id == remote.message_id) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            ChatEntry e   = remote;
            e.checksum   = computeChecksum(e); // recompute â€” don't trust sender
            entries_.push_back(e);

            // advance local clock past anything we've seen
            clock_.update(static_cast<uint64_t>(remote.lamport_time));
        }
    }

    // keep entries sorted by lamport_time for deterministic ordering
    std::sort(entries_.begin(), entries_.end(),
        [](const ChatEntry& a, const ChatEntry& b) {
            if (a.lamport_time != b.lamport_time)
                return a.lamport_time < b.lamport_time;
            // tiebreak by message_id for determinism across nodes
            return a.message_id < b.message_id;
        });
}

// ---------------------------------------------------------------
// Epoch
// ---------------------------------------------------------------
void LogStore::set_epoch(int32_t epoch) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_epoch_ = epoch;
}

// ---------------------------------------------------------------
// Checksum â€” payload only (metadata is mutable)
// ---------------------------------------------------------------
uint32_t LogStore::computeChecksum(const ChatEntry& e) const {
    return static_cast<uint32_t>(std::hash<std::string>{}(e.payload));
}
