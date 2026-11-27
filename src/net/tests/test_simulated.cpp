// Tests for src/net/simulated.*: the simulated network keeps what Transport promises, as the tests of the
// game online count on it.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "net/simulated.hpp"

using namespace lvr::net;

namespace {

std::vector<std::vector<std::uint8_t>> messages(const std::vector<Event>& events) {
    std::vector<std::vector<std::uint8_t>> bytes;
    for (const Event& event : events) {
        if (event.kind == Event::Kind::kMessage) {
            bytes.push_back(event.bytes);
        }
    }
    return bytes;
}

}  // namespace

TEST_CASE(
    "simulated network: reliable in order after the delay; fast never after a newer one, the newest always; "
    "who goes is seen to, after what he sent") {
    SimulatedNetwork network{{.delay_ms = 10, .jitter_ms = 8, .loss_percent = 30, .late_ms = 50, .behind_ms = 0}};
    auto host = network.host();
    auto guest = network.join();
    REQUIRE(host);
    REQUIRE(guest);
    auto other = network.join();  // (a second guest, who says nothing)
    REQUIRE(other);
    CHECK(host->hosting());
    CHECK_FALSE(guest->hosting());
    CHECK(host->poll().empty());  // (not yet: the delay)
    network.advance(18);          // (the delay and the most jitter)
    const std::vector<Event> joined = host->poll();
    REQUIRE(joined.size() == 2);
    CHECK(joined.front().kind == Event::Kind::kJoined);
    CHECK(joined.front().peer == 1);
    CHECK(joined.back().peer == 2);
    CHECK(host->round_trip_ms(1) == 20);
    CHECK(guest->now_ms() == 18);

    std::vector<Event> at_host;
    std::vector<Event> at_guest;
    for (std::uint8_t i = 0; i < 100; ++i) {
        guest->send(0, std::vector<std::uint8_t>{i}, Channel::kReliable);
        host->send(1, std::vector<std::uint8_t>{i}, Channel::kFast);
        network.advance(1);
        for (Event& event : host->poll()) {
            at_host.push_back(std::move(event));
        }
        for (Event& event : guest->poll()) {
            at_guest.push_back(std::move(event));
        }
    }
    network.advance(100);
    for (Event& event : host->poll()) {
        at_host.push_back(std::move(event));
    }
    for (Event& event : guest->poll()) {
        at_guest.push_back(std::move(event));
    }
    const auto reliable = messages(at_host);
    REQUIRE(reliable.size() == 100);
    for (std::uint8_t i = 0; i < 100; ++i) {
        CHECK(reliable.at(i).front() == i);
    }
    const auto fast = messages(at_guest);
    CHECK(fast.size() < 100);  // (lost ones overtaken by newer ones)
    for (std::size_t i = 1; i < fast.size(); ++i) {
        CHECK(fast.at(i).front() > fast.at(i - 1).front());
    }
    CHECK(fast.back().front() == 99);

    // The host lets the guest go: the guest has what was sent him first; the host's next poll has him gone.
    host->send(1, std::vector<std::uint8_t>{7}, Channel::kReliable);
    guest->send(0, std::vector<std::uint8_t>{8}, Channel::kReliable);
    host->drop(1);
    const std::vector<Event> dropped = host->poll();
    REQUIRE(dropped.size() == 1);
    CHECK(dropped.front().kind == Event::Kind::kLeft);
    network.advance(100);
    CHECK(messages(host->poll()).empty());  // (nothing more taken from him)
    const std::vector<Event> told = guest->poll();
    REQUIRE(told.size() == 2);
    CHECK(told.front().bytes == std::vector<std::uint8_t>{7});
    CHECK(told.back().kind == Event::Kind::kLeft);
    CHECK_FALSE(guest->round_trip_ms(0).has_value());

    // The host closes: the other guest sees him go, after what he sent.
    host->send(2, std::vector<std::uint8_t>{9}, Channel::kReliable);
    host.reset();
    network.advance(100);
    const std::vector<Event> gone = other->poll();
    REQUIRE(gone.size() == 2);
    CHECK(gone.front().bytes == std::vector<std::uint8_t>{9});
    CHECK(gone.back().kind == Event::Kind::kLeft);
    CHECK(network.join() == nullptr);  // (no host)
}
