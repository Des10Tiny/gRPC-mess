#include "messenger.grpc.pb.h"

#include <condition_variable>
#include <cstdlib>
#include <google/protobuf/util/time_util.h>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;

struct Message {
    std::string author_;
    std::string text_;
    google::protobuf::Timestamp time_;

    Message(std::string author, std::string text, google::protobuf::Timestamp time)
        : author_(std::move(author))
        , text_(std::move(text))
        , time_(std::move(time)) {
    }

    Message(const Message&) = default;
    Message(Message&&) = default;
    Message& operator=(const Message&) = default;
    Message& operator=(Message&&) = default;

    ~Message() = default;
};

class ClientQueue {
    std::queue<Message> messages_;
    std::mutex mtx_;
    std::condition_variable cv_;

public:
    void Push(Message msg) {
        {
            std::lock_guard lock(mtx_);
            messages_.push(std::move(msg));
        }

        cv_.notify_one();
    }

    std::optional<Message> WaitAndPop(std::chrono::milliseconds timeout = std::chrono::seconds(1)) {
        std::unique_lock lock(mtx_);

        bool have_message = cv_.wait_for(lock, timeout, [&] { return !messages_.empty(); });

        if (!have_message) {
            return std::nullopt;
        }

        Message msg = std::move(messages_.front());
        messages_.pop();

        return msg;
    }

    ClientQueue() = default;

    ClientQueue(const ClientQueue&) = delete;
    ClientQueue(ClientQueue&&) = delete;

    ClientQueue& operator=(const ClientQueue&) = delete;
    ClientQueue& operator=(ClientQueue&&) = delete;

    ~ClientQueue() = default;
};

class MessengerService final : public mes_grpc::MessengerServer::Service {
public:
    Status SendMessage(
        [[maybe_unused]] ServerContext* context,
        const mes_grpc::SendMessageRequest* request,
        mes_grpc::TimeResponse* response
    ) override {
        auto time_with_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::system_clock::now().time_since_epoch()
        )
                                   .count();

        auto time_for_curr_message =
            google::protobuf::util::TimeUtil::NanosecondsToTimestamp(time_with_nanos);
        *response->mutable_sendtime() = time_for_curr_message;

        Message curr_message = Message(request->author(), request->text(), time_for_curr_message);
        {
            std::lock_guard global_lock(clients_mtx_);

            for (const std::shared_ptr<ClientQueue>& it : clients_) {
                it->Push(curr_message);
            }
        }

        return Status::OK;
    }

    Status ReadMessages(
        ServerContext* context,
        [[maybe_unused]] const google::protobuf::Empty* request,
        ServerWriter<mes_grpc::ServerMessageResponse>* writer
    ) override {
        std::shared_ptr<ClientQueue> current_queue = std::make_shared<ClientQueue>();

        {
            std::lock_guard global_lock(clients_mtx_);
            clients_.push_back(current_queue);
        }

        while (!context->IsCancelled()) {
            std::optional<Message> new_message = current_queue->WaitAndPop();

            if (!new_message.has_value()) {
                continue;
            }

            mes_grpc::ServerMessageResponse new_message_proto;

            new_message_proto.set_author(new_message->author_);
            new_message_proto.set_text(new_message->text_);
            *new_message_proto.mutable_sendtime() = new_message->time_;

            if (!writer->Write(new_message_proto)) {
                break;
            }
        }

        {
            std::lock_guard global_lock(clients_mtx_);
            clients_.erase(
                std::remove(clients_.begin(), clients_.end(), current_queue), clients_.end()
            );
        }

        return Status::OK;
    }

private:
    std::mutex clients_mtx_;
    std::vector<std::shared_ptr<ClientQueue>> clients_;
};

void RunServer() {
    const char* port_env = std::getenv("MESSENGER_SERVER_PORT");
    std::string port = port_env ? port_env : "51075";

    std::string server_address = "0.0.0.0:" + port;

    MessengerService service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());

    server->Wait();
}

int main() {
    RunServer();
    return 0;
}