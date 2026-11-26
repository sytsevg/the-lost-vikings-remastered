// Tests for src/net/transport.*'s SDL3_net adapter: a host and a client in one process over 127.0.0.1 (ADR-0012). The
// messages come, in order, fast or reliably; with every datagram lost the fast ones come over TCP; a client that goes
// is seen to.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "net/transport.hpp"

using namespace lvr::net;

namespace {

constexpr std::uint16_t kFirstPort = 47231;

// A host on a port nothing else has, and a client joined to it.
struct Pair {
    std::unique_ptr<SdlTransport> host;
    std::unique_ptr<SdlTransport> client;
    std::vector<Event> at_host;
    std::vector<Event> at_client;

    Pair() {
        for (std::uint16_t port = kFirstPort; port < kFirstPort + 20 && !host; ++port) {
            if (auto opened = SdlTransport::host(port)) {
                host = std::move(*opened);
            }
        }
        REQUIRE(host);
        auto joined = SdlTransport::join("127.0.0.1", host->port(), 2000);
        REQUIRE(joined);
        client = std::move(*joined);
    }
    // Polls both until `done` says so, at most two seconds.
    bool until(const std::function<bool()>& done) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (std::chrono::steady_clock::now() < end) {
            for (Event& event : host->poll()) {
                at_host.push_back(std::move(event));
            }
            if (client) {
                for (Event& event : client->poll()) {
                    at_client.push_back(std::move(event));
                }
            }
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return false;
    }
    std::vector<std::vector<std::uint8_t>> messages(const std::vector<Event>& events) const {
        std::vector<std::vector<std::uint8_t>> bytes;
        for (const Event& event : events) {
            if (event.kind == Event::Kind::kMessage) {
                bytes.push_back(event.bytes);
            }
        }
        return bytes;
    }
};

}  // namespace

TEST_CASE(
    "transport: a client joins; messages go both ways, reliably and fast (by UDP and TCP, taken once), the ping") {
    Pair pair;
    REQUIRE(pair.until([&] { return !pair.at_host.empty(); }));
    CHECK(pair.at_host.front().kind == Event::Kind::kJoined);
    const int client = pair.at_host.front().peer;
    REQUIRE(pair.until([&] { return pair.host->fast_link(client) && pair.client->fast_link(0); }));
    pair.client->send(0, std::vector<std::uint8_t>{1, 2, 3}, Channel::kReliable);
    pair.client->send(0, std::vector<std::uint8_t>{4}, Channel::kFast);
    pair.host->send(client, std::vector<std::uint8_t>{9, 9}, Channel::kFast);
    // (A fast one goes by UDP and by TCP, the same numbered bytes: the first to come is taken, its copy not.)
    REQUIRE(pair.until(
        [&] { return pair.messages(pair.at_host).size() == 2 && pair.messages(pair.at_client).size() == 1; }));
    CHECK(std::ranges::count(pair.messages(pair.at_host), std::vector<std::uint8_t>{1, 2, 3}) == 1);
    CHECK(std::ranges::count(pair.messages(pair.at_host), std::vector<std::uint8_t>{4}) == 1);
    CHECK(pair.messages(pair.at_client).front() == std::vector<std::uint8_t>{9, 9});
    // Many: each taken once at most, never one after a newer one, and the last always (TCP is behind UDP).
    for (std::uint8_t i = 0; i < 50; ++i) {
        pair.host->send(client, std::vector<std::uint8_t>{i}, Channel::kFast);
    }
    REQUIRE(pair.until([&] { return pair.messages(pair.at_client).back() == std::vector<std::uint8_t>{49}; }));
    const auto got = pair.messages(pair.at_client);
    for (std::size_t i = 2; i < got.size(); ++i) {
        CHECK(got.at(i).front() > got.at(i - 1).front());
    }
    CHECK(pair.until([&] { return false; }) == false);  // (a while longer: nothing more comes)
    CHECK(pair.messages(pair.at_client).size() == got.size());
    REQUIRE(pair.until([&] { return pair.client->round_trip_ms(0).has_value(); }));
    CHECK(*pair.client->round_trip_ms(0) >= 0);
}

TEST_CASE("transport: with every datagram lost, the fast messages go over TCP") {
    Pair pair;
    pair.host->simulate_loss(100);
    pair.client->simulate_loss(100);
    REQUIRE(pair.until([&] { return !pair.at_host.empty(); }));
    const int client = pair.at_host.front().peer;
    CHECK_FALSE(pair.client->fast_link(0));
    for (std::uint8_t i = 0; i < 20; ++i) {
        pair.client->send(0, std::vector<std::uint8_t>{i}, Channel::kFast);
    }
    REQUIRE(pair.until([&] { return pair.messages(pair.at_host).size() == 20; }));
    const auto got = pair.messages(pair.at_host);
    for (std::uint8_t i = 0; i < 20; ++i) {
        CHECK(got.at(i) == std::vector<std::uint8_t>{i});  // in order: TCP's
    }
    CHECK_FALSE(pair.host->fast_link(client));
}

TEST_CASE("transport: a client that goes is seen to go") {
    Pair pair;
    REQUIRE(pair.until([&] { return !pair.at_host.empty(); }));
    pair.client.reset();
    REQUIRE(pair.until([&] {
        return std::ranges::any_of(pair.at_host, [](const Event& event) { return event.kind == Event::Kind::kLeft; });
    }));
}

TEST_CASE("transport: a client that falls silent, its connection not closed, is seen to go after a while") {
    Pair pair;
    REQUIRE(pair.until([&] { return !pair.at_host.empty(); }));
    pair.host->set_patience(1000);
    pair.client->simulate_cut();  // (a cable pulled: nothing it sends gets through, TCP included)
    REQUIRE(pair.until([&] {
        return std::ranges::any_of(pair.at_host, [](const Event& event) { return event.kind == Event::Kind::kLeft; });
    }));
}
