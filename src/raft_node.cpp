#include "raft_node.h"
#include <iostream>
#include <grpcpp/grpcpp.h>

RaftNode::RaftNode(const std::string& my_id, const std::map<std::string, std::string>& peer_addrs)
    : id_(my_id), peer_addrs_(peer_addrs) 
{
    // Initialize dummy entry for 1-based indexing safety
    LogEntry dummy;
    dummy.set_term(0);
    log_.push_back(dummy);

    connect_peers();

    last_heartbeat_ = std::chrono::steady_clock::now();
    background_thread_ = std::thread(&RaftNode::run_background_loop, this);
}

RaftNode::~RaftNode() {
    dead_.store(true);
    if (background_thread_.joinable()) {
        background_thread_.join();
    }
}

void RaftNode::connect_peers() {
    for (const auto& kv : peer_addrs_) {
        auto channel = grpc::CreateChannel(kv.second, grpc::InsecureChannelCredentials());
        peers_[kv.first] = ControlPlaneRaft::NewStub(channel);
    }
}

int RaftNode::get_random_timeout() const {
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(300, 600);
    return dist(rng);
}

int RaftNode::get_quorum() const {
    return ((peers_.size() + 1) / 2) + 1;
}

// === THE BACKGROUND LOOP ===
void RaftNode::run_background_loop() {
    while (!dead_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        std::lock_guard<std::mutex> lock(mtx_);
        
        // Safely apply any committed logs to the ACL
        apply_logs();

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_).count();

        if (role_ == RaftRole::Leader) {
            if (elapsed >= 50) { 
                send_heartbeats();
                last_heartbeat_ = now;
            }
        } else {
            if (elapsed > get_random_timeout()) { 
                start_election();
                last_heartbeat_ = now;
            }
        }
    }
}

// === The State Machine Execution ===
void RaftNode::apply_logs() {
    while (commit_index_ > last_applied_) {
        last_applied_++;
        const auto& entry = log_[last_applied_];
        
        if (entry.type() == LogEntry::ADD_USER) {
            authorized_users_.insert(entry.target_user_id());
            std::cout << "[ACL] Added user: " << entry.target_user_id() << "\n";
        } else if (entry.type() == LogEntry::REVOKE_USER) {
            authorized_users_.erase(entry.target_user_id());
            current_epoch_++; // Issue the new certificate!
            std::cout << "[ACL] Revoked user: " << entry.target_user_id() << ". New Epoch: " << current_epoch_ << "\n";
        }
    }
}

// === The ACL Hook ===
bool RaftNode::isAuthorized(const std::string& user_id, int32_t epoch) const {
    std::lock_guard<std::mutex> lock(mtx_);
    return (authorized_users_.count(user_id) > 0) && (epoch <= current_epoch_);
}

int32_t RaftNode::getCurrentEpoch() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return current_epoch_;
}

// === RAFT WRITE PATH ===
bool RaftNode::ProposeCommand(LogEntry::CommandType type, const std::string& target_user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (role_ != RaftRole::Leader) return false;

    LogEntry new_entry;
    new_entry.set_term(current_term_);
    new_entry.set_type(type);
    new_entry.set_target_user_id(target_user_id);
    
    log_.push_back(new_entry);
    match_index_[id_] = log_.size() - 1;
    
    send_heartbeats(); 
    return true;
}

// === CORE RAFT LOGIC ===
void RaftNode::start_election() {
    role_ = RaftRole::Candidate;
    current_term_++;
    voted_for_ = id_;
    
    auto votes_received = std::make_shared<std::atomic<int>>(1); 

    for (const auto& peer : peers_) {
        std::string peer_id = peer.first;
        
        RequestVoteArgs args;
        args.set_term(current_term_);
        args.set_candidate_id(id_);
        args.set_last_log_index(log_.size() - 1);
        args.set_last_log_term(log_.back().term());

        std::thread([this, peer_id, args, votes_received]() {
            RequestVoteReply reply;
            grpc::ClientContext ctx;
            auto status = peers_.at(peer_id)->RequestVote(&ctx, args, &reply);
            
            if (status.ok()) {
                std::lock_guard<std::mutex> lock(mtx_);
                if (role_ != RaftRole::Candidate || args.term() != current_term_) return;

                if (reply.term() > current_term_) {
                    current_term_ = reply.term();
                    role_ = RaftRole::Follower;
                    voted_for_ = "";
                    return;
                }

                if (reply.vote_granted()) {
                    (*votes_received)++;
                    if (*votes_received >= get_quorum()) {
                        role_ = RaftRole::Leader;
                        std::cout << "Won Election! Becoming Leader for Term " << current_term_ << "\n";
                        
                        int32_t last_log_index = log_.size() - 1;
                        for (const auto& p : peers_) {
                            next_index_[p.first] = last_log_index + 1;
                            match_index_[p.first] = 0;
                        }
                        match_index_[id_] = last_log_index;
                        send_heartbeats();
                    }
                }
            }
        }).detach();
    }
}

void RaftNode::send_heartbeats() {
    for (const auto& peer : peers_) {
        std::string peer_id = peer.first;
        
        AppendEntriesArgs args;
        args.set_term(current_term_);
        args.set_leader_id(id_);
        
        int32_t next_idx = next_index_[peer_id];
        int32_t prev_idx = next_idx - 1;
        
        args.set_prev_log_index(prev_idx);
        args.set_prev_log_term(log_[prev_idx].term());
        args.set_leader_commit(commit_index_); 

        for (size_t i = next_idx; i < log_.size(); i++) {
            *args.add_entries() = log_[i];
        }
        
        std::thread([this, peer_id, args]() {
            AppendEntriesReply reply;
            grpc::ClientContext ctx;
            auto status = peers_.at(peer_id)->AppendEntries(&ctx, args, &reply);
            
            if (status.ok()) {
                std::lock_guard<std::mutex> lock(mtx_);
                if (role_ != RaftRole::Leader || current_term_ != args.term()) return;

                if (reply.term() > current_term_) {
                    current_term_ = reply.term();
                    role_ = RaftRole::Follower;
                    voted_for_ = "";
                }
                
                if (reply.success()) {
                    int32_t new_match = args.prev_log_index() + args.entries_size();
                    if (new_match > match_index_[peer_id]) {
                        match_index_[peer_id] = new_match;
                        next_index_[peer_id] = match_index_[peer_id] + 1;
                        advance_commit_index();
                    }
                } else {
                    if (next_index_[peer_id] > 1) {
                        next_index_[peer_id]--;
                    }
                }
            }
        }).detach();
    }
}

void RaftNode::advance_commit_index() {
    for (int32_t n = log_.size() - 1; n > commit_index_; n--) {
        if (log_[n].term() != current_term_) continue;
        
        int replicas = 1;
        for (const auto& peer : peers_) {
            if (match_index_[peer.first] >= n) replicas++;
        }
        
        if (replicas >= get_quorum()) {
            commit_index_ = n;
            break; 
        }
    }
}

grpc::Status RaftNode::HandleRequestVote(const RequestVoteArgs* req, RequestVoteReply* resp) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (req->term() > current_term_) {
        current_term_ = req->term();
        role_ = RaftRole::Follower;
        voted_for_ = "";
    }

    if (req->term() < current_term_) {
        resp->set_term(current_term_);
        resp->set_vote_granted(false);
        return grpc::Status::OK;
    }

    int32_t my_last_idx = log_.size() - 1;
    int32_t my_last_term = log_[my_last_idx].term();
    
    bool is_up_to_date = false;
    if (req->last_log_term() > my_last_term) {
        is_up_to_date = true;
    } else if (req->last_log_term() == my_last_term && req->last_log_index() >= my_last_idx) {
        is_up_to_date = true;
    }

    bool can_vote = (voted_for_ == "" || voted_for_ == req->candidate_id());

    if (can_vote && is_up_to_date) {
        voted_for_ = req->candidate_id();
        role_ = RaftRole::Follower;
        last_heartbeat_ = std::chrono::steady_clock::now(); 
        resp->set_vote_granted(true);
    } else {
        resp->set_vote_granted(false);
    }
    
    resp->set_term(current_term_);
    return grpc::Status::OK;
}

grpc::Status RaftNode::HandleAppendEntries(const AppendEntriesArgs* req, AppendEntriesReply* resp) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (req->term() > current_term_) {
        current_term_ = req->term();
        role_ = RaftRole::Follower;
        voted_for_ = "";
    }

    if (req->term() < current_term_) {
        resp->set_term(current_term_);
        resp->set_success(false);
        return grpc::Status::OK;
    }

    role_ = RaftRole::Follower;
    last_heartbeat_ = std::chrono::steady_clock::now(); 

    if (req->prev_log_index() >= log_.size() || 
        log_[req->prev_log_index()].term() != req->prev_log_term()) {
        resp->set_term(current_term_);
        resp->set_success(false);
        return grpc::Status::OK;
    }

    int32_t insert_idx = req->prev_log_index() + 1;
    for (int i = 0; i < req->entries_size(); i++) {
        auto incoming_entry = req->entries(i);
        
        if (insert_idx < log_.size()) {
            if (log_[insert_idx].term() != incoming_entry.term()) {
                log_.erase(log_.begin() + insert_idx, log_.end()); 
                log_.push_back(incoming_entry);
            }
        } else {
            log_.push_back(incoming_entry);
        }
        insert_idx++;
    }
    
    if (req->leader_commit() > commit_index_) {
        commit_index_ = std::min(req->leader_commit(), (int32_t)(log_.size() - 1));
    }

    resp->set_success(true);
    return grpc::Status::OK;
}

grpc::Status RaftNode::HandleListUsers(const ListUsersRequest* req, ListUsersResponse* resp) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    for (const auto& user : authorized_users_) {
        resp->add_authorized_users(user);
    }
    resp->set_current_epoch(current_epoch_);
    
    return grpc::Status::OK;
}