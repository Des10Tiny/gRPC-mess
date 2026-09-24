#include "httplib.h"
#include "messenger.grpc.pb.h"
#include "messenger.pb.h"
#include "nlohmann/json.hpp"

#include <cstdlib>
#include <google/protobuf/util/time_util.h>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class ClientState {
public:
    ClientState(std::unique_ptr<mes_grpc::MessengerServer::Stub> stub)
        : stub_(std::move(stub)) {
        StartListenServer();
    }

    void SendMessage(const httplib::Request& request, httplib::Response& response) {
        auto parsed_result = nlohmann::json::parse(request.body);

        std::string author = parsed_result["author"].get<std::string>();
        std::string text = parsed_result["text"].get<std::string>();

        grpc::ClientContext context;

        mes_grpc::SendMessageRequest send_message;
        send_message.set_author(std::move(author));
        send_message.set_text(std::move(text));

        ::mes_grpc::TimeResponse time_response;

        stub_->SendMessage(&context, send_message, &time_response);

        auto answer_time = google::protobuf::util::TimeUtil::ToString(time_response.sendtime());

        auto our_answer = nlohmann::json();
        our_answer["sendTime"] = answer_time;

        response.set_content(our_answer.dump(), "application/json");
    }

    void GetAndFlushMessages(const httplib::Request& request, httplib::Response& response) {
        std::vector<mes_grpc::ServerMessageResponse> local_buffer_to_ans;

        {
            std::lock_guard lock(mtx_);
            local_buffer_to_ans = std::move(buffer_);
        }
    }

private:
    void ReaderThreadHelper() {
        grpc::ClientContext context;

        auto reader_pipe = stub_->ReadMessages(&context, ::google::protobuf::Empty{});

        mes_grpc::ServerMessageResponse message;
        while (reader_pipe->Read(&message)) {
            {
                std::lock_guard lock(mtx_);
                buffer_.push_back(std::move(message));
            }
        }
    }

    void StartListenServer() {
        std::thread t([this]() { this->ReaderThreadHelper(); });
        t.detach();
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

    httplib::Server svr;

    svr.Post(
        "/sendMessage", [&app_state](const httplib::Request& request, httplib::Response& response) {
            app_state.SendMessage(request, response);
        }
    );
    svr.Post(
        "/getAndFlushMessages",
        [&app_state](const httplib::Request& request, httplib::Response& response) {
            app_state.GetAndFlushMessages(request, response);
        }
    );

    svr.listen("0.0.0.0", static_cast<int>(*http_port));
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