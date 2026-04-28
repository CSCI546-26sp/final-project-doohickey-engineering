#pragma once

#include <memory>
#include <string>
#include <vector>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>

#include "locality_messaging.grpc.pb.h"
#include "log_store.h"

class DataPlaneGossipImpl final : public locality_messaging::DataPlaneGossip::Service {
public:
    DataPlaneGossipImpl(const std::string& node_id,
                        const std::string& auth_address);

    grpc::Status SyncLogs(
        grpc::ServerContext* ctx,
        const locality_messaging::SyncLogsRequest* req,
        locality_messaging::SyncLogsResponse* resp
    ) override;

private:
    std::string node_id_;
    LogStore store_;

    std::unique_ptr<locality_messaging::IntegrationAuth::Stub> auth_stub_;

    bool checkAuth(const std::string& user_id, int32_t epoch);
};