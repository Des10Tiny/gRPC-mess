#include "httplib.h"
#include "messenger.grpc.pb.h"
#include "messenger.pb.h"
#include "nlohmann/json.hpp"

#include <chrono>
#include <cstdlib>
#include <google/protobuf/util/time_util.h>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <mutex>
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
        context.set_wait_for_ready(true);

        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
        context.set_deadline(deadline);

        mes_grpc::SendMessageRequest send_message;
        send_message.set_author(std::move(author));
        send_message.set_text(std::move(text));

        ::mes_grpc::TimeResponse time_response;

        auto status = stub_->SendMessage(&context, send_message, &time_response);

        if (!status.ok()) {
            response.status = 500;
            response.set_content("gRPC Error", "text/plain");

            return;
        }

        auto answer_time = google::protobuf::util::TimeUtil::ToString(time_response.sendtime());

        auto our_answer = nlohmann::json();
        our_answer["sendTime"] = answer_time;

        response.set_content(our_answer.dump(), "application/json");
    }

    void GetAndFlushMessages(
        [[maybe_unused]] const httplib::Request& request, httplib::Response& response
    ) {
        std::vector<mes_grpc::ServerMessageResponse> local_buffer_to_ans;

        {
            std::lock_guard lock(mtx_);
            local_buffer_to_ans = std::move(buffer_);
        }

        nlohmann::json arr_to_send = nlohmann::json::array();

        for (auto& one_message_it : local_buffer_to_ans) {
            auto one_message = nlohmann::json();

            one_message["author"] = std::move(*one_message_it.mutable_author());
            one_message["text"] = std::move(*one_message_it.mutable_text());
            one_message["sendTime"] =
                google::protobuf::util::TimeUtil::ToString(one_message_it.sendtime());

            arr_to_send.push_back(one_message);
        }

        response.set_content(arr_to_send.dump(), "application/json");
    }

private:
    void ReaderThreadHelper() {
        grpc::ClientContext context;
        context.set_wait_for_ready(true);

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
    const char* env_addr = std::getenv("MESSENGER_SERVER_ADDR");
    const char* server_addr = env_addr ? env_addr : "localhost:51075";

    const char* env_port = std::getenv("MESSENGER_HTTP_PORT");
    const char* http_port = env_port ? env_port : "8080";

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

    svr.listen("0.0.0.0", std::stoi(http_port));
}

int main() {
    try {
        RunServer();
    } catch (...) {
        std::cerr << "Unkown error";
        return 1;
    }

    return 0;
}