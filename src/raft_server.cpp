#include <iostream>
#include <memory>
#include <string>
#include <map>

#include <grpcpp/grpcpp.h>
#include "locality_messaging.grpc.pb.h"
#include "raft_node.h"

using namespace locality_messaging;
using grpc::ServerContext;
using grpc::Status;

// --- Service 1: IntegrationAuth (For the Data Plane) ---
class IntegrationAuthImpl final : public IntegrationAuth::Service {
    std::shared_ptr<RaftNode> node_;
public:
    explicit IntegrationAuthImpl(std::shared_ptr<RaftNode> node) : node_(node) {}

    Status CheckAuthorization(ServerContext* context, 
                              const AuthCheckRequest* req, 
                              AuthCheckResponse* resp) override {
        bool valid = node_->isAuthorized(req->user_id(), req->epoch());
        resp->set_is_authorized(valid);
        return Status::OK;
    }
};

// --- Service 2: ControlPlaneRaft (For Peers & Clients) ---
class ControlPlaneRaftImpl final : public ControlPlaneRaft::Service {
    std::shared_ptr<RaftNode> node_;
public:
    explicit ControlPlaneRaftImpl(std::shared_ptr<RaftNode> node) : node_(node) {}

    // --- Admin Endpoints ---
    Status AddUser(ServerContext* context, const AddUserRequest* req, AddUserResponse* resp) override {
        bool is_leader = node_->ProposeCommand(LogEntry::ADD_USER, req->user_id());
        resp->set_success(is_leader);
        resp->set_current_epoch(node_->getCurrentEpoch());
        return Status::OK;
    }

    Status RevokeUser(ServerContext* context, const RevokeUserRequest* req, RevokeUserResponse* resp) override {
        bool is_leader = node_->ProposeCommand(LogEntry::REVOKE_USER, req->user_id());
        resp->set_success(is_leader);
        // Note: Real epoch increment happens after commit, returning current for now
        resp->set_new_epoch(node_->getCurrentEpoch()); 
        return Status::OK;
    }

    Status ListUsers(ServerContext* context, const ListUsersRequest* req, ListUsersResponse* resp) override {
        return node_->HandleListUsers(req, resp);
    }

    Status GetCertificate(ServerContext* context,
                          const CertificateRequest* req,
                          CertificateResponse* resp) override {
        resp->set_user_id(req->user_id());
        resp->set_epoch(node_->getCurrentEpoch());
        return Status::OK;
    }

    // --- Internal Raft RPCs ---
    Status RequestVote(ServerContext* context, const RequestVoteArgs* req, RequestVoteReply* resp) override {
        return node_->HandleRequestVote(req, resp);
    }
    
    Status AppendEntries(ServerContext* context, const AppendEntriesArgs* req, AppendEntriesReply* resp) override {
        return node_->HandleAppendEntries(req, resp);
    }
};

int main(int argc, char** argv) {
    // Usage: ./raft_server <node_id> <listen_address> <peer1_id=peer1_addr> [peer2_id=peer2_addr] ...
    // 
    // Examples:
    //   # 3-node cluster
    //   ./raft_server auth-1 0.0.0.0:50053 auth-2=localhost:50054 auth-3=localhost:50055
    //   ./raft_server auth-2 0.0.0.0:50054 auth-1=localhost:50053 auth-3=localhost:50055
    //   
    //   # 10-node cluster
    //   ./raft_server auth-1 0.0.0.0:50053 auth-2=localhost:50054 auth-3=localhost:50055 ... auth-10=localhost:50062
    
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <node_id> <listen_address> <peer1_id=peer1_addr> [peer2_id=peer2_addr] ...\n";
        std::cerr << "\nExample:\n";
        std::cerr << "  " << argv[0] << " auth-1 0.0.0.0:50053 auth-2=localhost:50054 auth-3=localhost:50055\n";
        return 1;
    }
    
    std::string node_id = argv[1];
    std::string addr = argv[2];
    
    // Parse peers from command-line arguments (format: peer_id=peer_address)
    std::map<std::string, std::string> peers;
    std::cout << "Node: " << node_id << "\nPeers:\n";
    
    for (int i = 3; i < argc; ++i) {
        std::string peer_spec = argv[i];
        size_t eq_pos = peer_spec.find('=');
        
        if (eq_pos == std::string::npos) {
            std::cerr << "Invalid peer specification: " << peer_spec << " (expected format: peer_id=address)\n";
            return 1;
        }
        
        std::string peer_id = peer_spec.substr(0, eq_pos);
        std::string peer_addr = peer_spec.substr(eq_pos + 1);
        
        if (peer_id == node_id) {
            std::cerr << "Error: Node cannot be its own peer: " << peer_id << "\n";
            return 1;
        }
        
        peers[peer_id] = peer_addr;
        std::cout << "  " << peer_id << " -> " << peer_addr << "\n";
    }
    
    if (peers.empty()) {
        std::cerr << "Error: At least one peer must be specified\n";
        return 1;
    }
    
    auto raft_node = std::make_shared<RaftNode>(node_id, peers);
    
    IntegrationAuthImpl auth_service(raft_node);
    ControlPlaneRaftImpl raft_service(raft_node);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&auth_service);
    builder.RegisterService(&raft_service);
    
    auto server = builder.BuildAndStart();
    std::cout << "Control Plane Raft Server [" << node_id << "] listening on " << addr << "\n";
    server->Wait();
    
    return 0;
}