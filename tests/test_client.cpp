#include "doctest.h"
#include "httplib.h"
#include "subprocess.hpp"
#include <chrono>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef SERVER_EXEC
#define SERVER_EXEC "../messenger_server"
#endif
#ifndef CLIENT_EXEC
#define CLIENT_EXEC "../messenger_client"
#endif

using Json = nlohmann::json;

void ParseAddr(const std::string& addr, std::string& host, int& port) {
    auto colon = addr.find(':');
    host = addr.substr(0, colon);
    port = std::stoi(addr.substr(colon + 1));
}

void WaitForHttp(const std::string& host, int port) {
    httplib::Client cli(host, port);
    cli.set_connection_timeout(1, 0);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto res = cli.Get("/")) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    FAIL("Timed out waiting for HTTP endpoint " << host << ":" << port);
}

Json PostJson(httplib::Client& cli, const std::string& path, const Json& body = Json::object()) {
    std::string body_str = body.empty() ? "" : body.dump();
    auto res = cli.Post(path, body_str, "application/json");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    if (res->body.empty()) {
        return Json();
    }
    return Json::parse(res->body);
}

void FlushMessages(httplib::Client& cli) {
    PostJson(cli, "/getAndFlushMessages");
}

std::vector<Json> WaitForMessages(httplib::Client& cli, const std::vector<Json>& expected) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::vector<Json> messages;
    while (messages.size() < expected.size()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        auto batch = PostJson(cli, "/getAndFlushMessages");
        REQUIRE(batch.is_array());
        for (const auto& msg : batch) {
            messages.push_back(msg);
        }
        for (size_t i = 0; i < messages.size(); ++i) {
            REQUIRE(messages[i] == expected[i]);
        }
        if (messages.size() < expected.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    return messages;
}

void WaitForClientsReady(httplib::Client& cli1, httplib::Client& cli2) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    int attempt = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        Json probe = {{"author", "StreamProbe"}, {"text", "probe #" + std::to_string(attempt++)}};
        PostJson(cli1, "/sendMessage", probe);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto batch1 = PostJson(cli1, "/getAndFlushMessages");
        auto batch2 = PostJson(cli2, "/getAndFlushMessages");

        bool cli1_ready = false, cli2_ready = false;
        if (batch1.is_array()) {
            for (const auto& msg : batch1) {
                if (msg.value("author", "") == "StreamProbe") {
                    cli1_ready = true;
                }
            }
        }
        if (batch2.is_array()) {
            for (const auto& msg : batch2) {
                if (msg.value("author", "") == "StreamProbe") {
                    cli2_ready = true;
                }
            }
        }

        if (cli1_ready && cli2_ready) {
            FlushMessages(cli1);
            FlushMessages(cli2);
            return;
        }
    }
    FAIL("Clients streams did not become ready in time");
}

struct ClientFixture {
    httplib::Client cli;
    ClientFixture(const std::string& host, int port)
        : cli(host, port) {
        cli.set_read_timeout(5, 0);
        cli.set_connection_timeout(1, 0);
        FlushMessages(cli);
    }
    ~ClientFixture() {
        FlushMessages(cli);
    }
};

TEST_CASE("Client Tests") {
    SubProcess server_proc(SERVER_EXEC);
    SubProcess client1_proc(std::string("env MESSENGER_HTTP_PORT=8080 ") + CLIENT_EXEC);
    SubProcess client2_proc(std::string("env MESSENGER_HTTP_PORT=8081 ") + CLIENT_EXEC);

    const char* env1 = std::getenv("MESSENGER_TEST_CLIENT1_ADDR");
    std::string addr1 = env1 ? env1 : "127.0.0.1:8080";
    std::string host1;
    int port1;
    ParseAddr(addr1, host1, port1);

    const char* env2 = std::getenv("MESSENGER_TEST_CLIENT2_ADDR");
    std::string addr2 = env2 ? env2 : "127.0.0.1:8081";
    std::string host2;
    int port2;
    ParseAddr(addr2, host2, port2);

    WaitForHttp(host1, port1);
    WaitForHttp(host2, port2);

    ClientFixture client1(host1, port1);
    ClientFixture client2(host2, port2);

    WaitForClientsReady(client1.cli, client2.cli);

    SUBCASE("test_single_client_single_message") {
        Json mes = {{"author", "TestSingleClient"}, {"text", "This is test text"}};
        auto resp = PostJson(client1.cli, "/sendMessage", mes);
        mes["sendTime"] = resp["sendTime"];

        std::vector<Json> expected = {mes};
        CHECK(WaitForMessages(client1.cli, expected) == expected);
        CHECK(WaitForMessages(client2.cli, expected) == expected);
    }

    SUBCASE("test_single_client_multiple_messages") {
        std::vector<Json> mes = {
            {{"author", "TestSingleClient1"}, {"text", "This is test text"}},
            {{"author", "TestSingleClient2"}, {"text", "This is test text"}}
        };
        for (auto& m : mes) {
            auto resp = PostJson(client1.cli, "/sendMessage", m);
            m["sendTime"] = resp["sendTime"];
        }
        CHECK(WaitForMessages(client1.cli, mes) == mes);
        CHECK(WaitForMessages(client2.cli, mes) == mes);
    }

    SUBCASE("test_two_clients_multiple_messages") {
        std::string client1_name = "TestMultiClient1";
        std::string client2_name = "TestMultiClient2";
        std::vector<Json> mes = {
            {{"author", client1_name}, {"text", "This is test text #1"}},
            {{"author", client1_name}, {"text", "This is test text #2"}},
            {{"author", client2_name}, {"text", "This is test text #3"}},
            {{"author", client2_name}, {"text", "This is test text #4"}}
        };

        std::set<std::string> times;
        for (auto& m : mes) {
            auto& cli = (m["author"] == client1_name) ? client1.cli : client2.cli;
            auto resp = PostJson(cli, "/sendMessage", m);
            m["sendTime"] = resp["sendTime"];
            times.insert(resp["sendTime"].get<std::string>());
        }

        CHECK(times.size() == mes.size());
        CHECK(WaitForMessages(client1.cli, mes) == mes);
        CHECK(WaitForMessages(client2.cli, mes) == mes);
    }
}