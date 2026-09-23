#include "messenger.grpc.pb.h"

#include <condition_variable>
#include <google/protobuf/util/time_util.h>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
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
    std::mutex mtx_;
    std::queue<Message> messages_;
    std::condition_variable cv_;

public:
    void Push(Message msg) {
        {
            std::lock_guard lock(mtx_);
            messages_.push(std::move(msg));
        }

        cv_.notify_one();
    }

    Message WaitAndPop() {
        std::unique_lock lock(mtx_);
        cv_.wait(lock, [&] { return !messages_.empty(); });

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
        [[maybe_unused]] const mes_grpc::SendMessageRequest* request,
        [[maybe_unused]] mes_grpc::TimeResponse* response
    ) override {
        return Status::OK;
    }

    Status ReadMessages(
        [[maybe_unused]] ServerContext* context,
        [[maybe_unused]] const google::protobuf::Empty* request,
        [[maybe_unused]] ServerWriter<mes_grpc::ServerMessageResponse>* writer
    ) override {
        return Status::OK;
    }

private:
    std::mutex clients_mtx_;
    std::vector<std::shared_ptr<ClientQueue>> clients_;
};
