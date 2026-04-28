#include "data_plane_service.h"
#include <grpcpp/server_builder.h>
#include <iostream>

using grpc::ServerBuilder;

void RunServer(const std::string& node_id,
               const std::string& listen_address,
               const std::string& auth_address) {
    DataPlaneGossipImpl service(node_id, auth_address);

    ServerBuilder builder;
    builder.AddListeningPort(listen_address,
                             grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    std::cerr << "Listening on " << listen_address << std::endl;
    server->Wait();
}

int main(int argc, char** argv) {
    std::string node_id     = argc > 1 ? argv[1] : "node-1";
    std::string listen_addr = argc > 2 ? argv[2] : "0.0.0.0:50051";
    std::string auth_addr   = argc > 3 ? argv[3] : "localhost:50053";

    RunServer(node_id, listen_addr, auth_addr);
}