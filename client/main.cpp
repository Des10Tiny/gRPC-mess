#include "messenger.grpc.pb.h"
#include "messenger.pb.h"

#include <cstdlib>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

class ClientState {
public:
    ClientState(std::unique_ptr<mes_grpc::MessengerServer::Stub> stub)
        : stub_(std::move(stub)) {
        StartListenServer();
    }

private:
    void StartListenServer() {
    }
    std::vector<mes_grpc::ServerMessageResponse> buffer_;
    std::mutex mtx_;
    std::unique_ptr<mes_grpc::MessengerServer::Stub> stub_;
};

void RunServer() {
    const char* server_addr = std::getenv("MESSENGER_SERVER_ADDR");
    const char* http_port = std::getenv("MESSENGER_HTTP_PORT");

    if (!server_addr || !http_port) {
        throw std::runtime_error("MESSENGER_SERVER_ADDR or MESSENGER_HTTP_PORT not provided");
    }

    [[maybe_unused]] std::string str_server_addr = "0.0.0.0:" + std::string(server_addr);

    auto channel = grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials());
    auto pointer_stub = mes_grpc::MessengerServer::NewStub(channel);

    ClientState app_state(std::move(pointer_stub));
}

int main() {
    try {
        RunServer();
    } catch (const std::exception& e) {
        std::cerr << e.what();
        return 1;
    } catch (...) {
        std::cerr << "Unkown error";
        return 2;
    }

    return 0;
}