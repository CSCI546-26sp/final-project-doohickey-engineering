// src/fake_auth_server.cpp — always authorizes everyone
#include <grpcpp/grpcpp.h>
#include "locality_messaging.grpc.pb.h"

using namespace locality_messaging;

class FakeAuth final : public IntegrationAuth::Service {
    grpc::Status CheckAuthorization(grpc::ServerContext*,
                                    const AuthCheckRequest*,
                                    AuthCheckResponse* resp) override {
        resp->set_is_authorized(true);
        return grpc::Status::OK;
    }
};

int main(int argc, char** argv) {
    std::string addr = argc > 1 ? argv[1] : "0.0.0.0:50053";
    FakeAuth svc;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&svc);
    auto server = builder.BuildAndStart();
    std::cout << "Fake auth listening on " << addr << "\n";
    server->Wait();
}