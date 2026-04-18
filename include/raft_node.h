#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <random>


#include "locality_messaging.grpc.pb.h"

using namespace locality_messaging;

enum class RaftRole { Follower, Candidate, Leader };

class RaftNode {
public:
    RaftNode(const std::string& my_id, const std::map<std::string, std::string>& peer_addrs);
    ~RaftNode();

    // --- State Machine Hooks (Read Path) ---
    bool isAuthorized(const std::string& user_id, int32_t epoch) const;
    int32_t getCurrentEpoch() const;

    // --- Admin Commands (Write Path) ---
    bool ProposeCommand(LogEntry::CommandType type, const std::string& target_user_id);

    // --- Raft RPC Handlers ---
    grpc::Status HandleRequestVote(const RequestVoteArgs* req, RequestVoteReply* resp);
    grpc::Status HandleAppendEntries(const AppendEntriesArgs* req, AppendEntriesReply* resp);
    grpc::Status HandleListUsers(const ListUsersRequest* req, ListUsersResponse* resp);

private:
    std::string id_;
    std::map<std::string, std::string> peer_addrs_;
    std::unordered_map<std::string, std::unique_ptr<ControlPlaneRaft::Stub>> peers_;

    mutable std::mutex mtx_;
    std::atomic<bool> dead_{false};
    std::thread background_thread_;

    // --- Persistent Raft State ---
    int32_t current_term_{0};
    std::string voted_for_{""};
    std::vector<LogEntry> log_; 

    // --- Volatile Raft State ---
    std::atomic<RaftRole> role_{RaftRole::Follower};
    std::chrono::steady_clock::time_point last_heartbeat_;
    int32_t commit_index_{0};
    int32_t last_applied_{0};

    // --- Volatile Leader State ---
    std::unordered_map<std::string, int32_t> next_index_;
    std::unordered_map<std::string, int32_t> match_index_;

    // --- The ACL State Machine ---
    int32_t current_epoch_{1};
    std::unordered_set<std::string> authorized_users_;

    // --- Internal Helpers ---
    void connect_peers();
    void run_background_loop();
    void apply_logs();
    void advance_commit_index();
    void start_election();
    void send_heartbeats();
    int get_quorum() const;
    int get_random_timeout() const;
};