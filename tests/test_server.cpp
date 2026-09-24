#include "doctest.h"
#include "messenger.grpc.pb.h"
#include "subprocess.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <google/protobuf/util/time_util.h>
#include <grpcpp/grpcpp.h>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

#ifndef SERVER_EXEC
#define SERVER_EXEC "../messenger_server"
#endif

class MessageStream {
    std::unique_ptr<mes_grpc::MessengerServer::Stub> stub_;
    grpc::ClientContext context_;
    std::unique_ptr<grpc::ClientReader<mes_grpc::ServerMessageResponse>> reader_;
    std::queue<mes_grpc::ServerMessageResponse> messages_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread reader_thread_;
    bool is_closed_ = false;

    void ReadLoop() {
        mes_grpc::ServerMessageResponse msg;
        while (reader_->Read(&msg)) {
            std::lock_guard<std::mutex> lock(mtx_);
            messages_.push(msg);
            cv_.notify_one();
        }
    }

public:
    MessageStream(std::shared_ptr<grpc::Channel> channel) {
        stub_ = mes_grpc::MessengerServer::NewStub(channel);
        context_.set_wait_for_ready(true);

        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(60);
        context_.set_deadline(deadline);

        reader_ = stub_->ReadMessages(&context_, google::protobuf::Empty());
        reader_thread_ = std::thread(&MessageStream::ReadLoop, this);
    }

    std::optional<mes_grpc::ServerMessageResponse> TryGetMessage(int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
                return !messages_.empty();
            })) {
            auto msg = messages_.front();
            messages_.pop();
            return msg;
        }
        return std::nullopt;
    }

    std::vector<mes_grpc::ServerMessageResponse> ReadMessages(int count, int timeout_ms = 10000) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        std::vector<mes_grpc::ServerMessageResponse> res;
        while (res.size() < static_cast<size_t>(count)) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 deadline - std::chrono::steady_clock::now()
            )
                                 .count();
            REQUIRE(remaining > 0);
            std::unique_lock<std::mutex> lock(mtx_);
            bool received = cv_.wait_for(lock, std::chrono::milliseconds(remaining), [this] {
                return !messages_.empty();
            });
            REQUIRE(received);
            res.push_back(messages_.front());
            messages_.pop();
        }
        return res;
    }

    void Close() {
        if (is_closed_) {
            return;
        }
        is_closed_ = true;
        context_.TryCancel();
        if (reader_thread_.joinable()) {
            reader_thread_.join();
        }
    }

    ~MessageStream() {
        Close();
    }
};

mes_grpc::TimeResponse SendMessage(
    std::shared_ptr<grpc::Channel> channel, const std::string& author, const std::string& text
) {
    auto stub = mes_grpc::MessengerServer::NewStub(channel);
    grpc::ClientContext context;
    context.set_wait_for_ready(true);

    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
    context.set_deadline(deadline);

    mes_grpc::SendMessageRequest req;
    req.set_author(author);
    req.set_text(text);

    mes_grpc::TimeResponse resp;
    grpc::Status status = stub->SendMessage(&context, req, &resp);
    REQUIRE(status.ok());

    return resp;
}

std::vector<std::vector<mes_grpc::ServerMessageResponse>>
WaitForStreams(std::shared_ptr<grpc::Channel> channel, std::vector<MessageStream*> streams) {
    std::vector<std::vector<mes_grpc::ServerMessageResponse>> preceding(streams.size());

    for (int attempt = 0; attempt < 10; ++attempt) {
        std::string text = "probe #" + std::to_string(attempt);
        SendMessage(channel, "StreamProbe", text);

        bool streams_ready = true;
        for (size_t i = 0; i < streams.size(); ++i) {
            bool found_probe = false;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (std::chrono::steady_clock::now() < deadline) {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     deadline - std::chrono::steady_clock::now()
                )
                                     .count();
                if (remaining <= 0) {
                    break;
                }

                auto msg_opt = streams[i]->TryGetMessage(remaining);
                if (msg_opt) {
                    if (msg_opt->author() == "StreamProbe" && msg_opt->text() == text) {
                        found_probe = true;
                        break;
                    } else {
                        preceding[i].push_back(*msg_opt);
                    }
                }
            }
            if (!found_probe) {
                streams_ready = false;
            }
        }

        if (streams_ready) {
            for (auto& vec : preceding) {
                vec.erase(
                    std::remove_if(
                        vec.begin(),
                        vec.end(),
                        [](const mes_grpc::ServerMessageResponse& m) {
                            return m.author() == "StreamProbe";
                        }
                    ),
                    vec.end()
                );
            }
            return preceding;
        }
    }
    FAIL("ReadMessages streams did not become ready");
    return preceding;
}

bool CompareMsgs(
    const mes_grpc::TimeResponse& sent,
    const std::string& author,
    const std::string& text,
    const mes_grpc::ServerMessageResponse& recv
) {
    return recv.author() == author && recv.text() == text &&
           google::protobuf::util::TimeUtil::TimestampToNanoseconds(recv.sendtime()) ==
               google::protobuf::util::TimeUtil::TimestampToNanoseconds(sent.sendtime());
}

TEST_CASE("Server Tests") {
    SubProcess server_proc(SERVER_EXEC);

    const char* env_addr = std::getenv("MESSENGER_TEST_SERVER_ADDR");
    std::string server_addr = env_addr ? env_addr : "127.0.0.1:51075";

    auto channel = grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials());

    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(20);
    bool connected = channel->WaitForConnected(deadline);
    REQUIRE(connected);

    SUBCASE("test_send_smoke") {
        SendMessage(channel, "alice", "hello");
    }

    SUBCASE("test_send_returns_ascending_time") {
        std::vector<mes_grpc::TimeResponse> outputs;
        outputs.reserve(10);
        for (int i = 0; i < 10; ++i) {
            outputs.push_back(SendMessage(channel, "alice", "hello"));
        }
        for (size_t i = 0; i < outputs.size() - 1; ++i) {
            auto t1 =
                google::protobuf::util::TimeUtil::TimestampToNanoseconds(outputs[i].sendtime());
            auto t2 =
                google::protobuf::util::TimeUtil::TimestampToNanoseconds(outputs[i + 1].sendtime());
            CHECK(t1 < t2);
        }
    }

    SUBCASE("test_get_messages_smoke") {
        MessageStream stream(channel);
        WaitForStreams(channel, {&stream});
        auto sent_time = SendMessage(channel, "alice", "hello");
        auto received = stream.ReadMessages(1);
        REQUIRE(received.size() == 1);
        CHECK(CompareMsgs(sent_time, "alice", "hello", received[0]));
    }

    SUBCASE("test_get_only_sends_new") {
        int n1 = 2, n2 = 3, n3 = 4;

        std::vector<mes_grpc::TimeResponse> sent1;
        {
            MessageStream stream(channel);
            auto preceding = WaitForStreams(channel, {&stream});
            REQUIRE(preceding[0].empty());

            for (int i = 0; i < n1; ++i) {
                sent1.push_back(SendMessage(channel, "alice", "hello"));
            }

            auto received1 = stream.ReadMessages(n1);
            REQUIRE(received1.size() == sent1.size());
            for (size_t i = 0; i < sent1.size(); ++i) {
                CHECK(CompareMsgs(sent1[i], "alice", "hello", received1[i]));
            }
        }

        for (int i = 0; i < n2; ++i) {
            SendMessage(channel, "alice", "hello");
        }

        std::vector<mes_grpc::TimeResponse> sent3;
        {
            MessageStream stream(channel);
            auto preceding = WaitForStreams(channel, {&stream});
            REQUIRE(preceding[0].empty());

            for (int i = 0; i < n3; ++i) {
                sent3.push_back(SendMessage(channel, "alice", "hello"));
            }

            auto received3 = stream.ReadMessages(n3);
            REQUIRE(received3.size() == sent3.size());
            for (size_t i = 0; i < sent3.size(); ++i) {
                CHECK(CompareMsgs(sent3[i], "alice", "hello", received3[i]));
            }
        }
    }
}