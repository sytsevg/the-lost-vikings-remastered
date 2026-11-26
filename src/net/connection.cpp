#include "net/connection.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace lvr::net {

namespace {

constexpr std::uint32_t kEchoEvery = 500;  // milliseconds
constexpr int kQuitWaitMs = 500;

}  // namespace

Connection::Connection(std::unique_ptr<Transport> transport, Identity identity)
    : transport_{std::move(transport)}, identity_{std::move(identity)} {}

std::optional<std::string> Connection::refusal(const Hello& hello) const {
    if (hello.protocol != kProtocol || hello.state_version != identity_.state_version) {
        return "another version of the game";
    }
    if (hello.build != identity_.build) {
        return std::format("another build ({}; the host's is {})", hello.build, identity_.build);
    }
    if (hello.game != identity_.game) {
        return "other game files";
    }
    return std::nullopt;
}

Hello Connection::hello() const {
    return Hello{.protocol = kProtocol,
                 .state_version = identity_.state_version,
                 .build = identity_.build,
                 .game = identity_.game,
                 .name = identity_.name};
}

void Connection::receive() {
    for (Event& event : transport_->poll()) {
        const int player = player_of(event.peer);
        if (event.kind == Event::Kind::kJoined || event.kind == Event::Kind::kLeft) {
            inbox_.push_back(
                {.kind = event.kind == Event::Kind::kJoined ? Incoming::Kind::kJoined : Incoming::Kind::kLeft,
                 .peer = event.peer,
                 .player = player,
                 .message = {}});
            continue;
        }
        if (const std::optional<Message> message = decode(event.bytes)) {
            for (Message& one : unbundled(*message)) {
                inbox_.push_back({.kind = Incoming::Kind::kMessage,
                                  .peer = event.peer,
                                  .player = player,
                                  .message = std::move(one)});
            }
        }
    }
}

std::optional<Incoming> Connection::next() {
    while (!inbox_.empty()) {
        Incoming incoming = std::move(inbox_.front());
        inbox_.pop_front();
        if (incoming.kind == Incoming::Kind::kMessage) {
            if (const auto* echo = std::get_if<Echo>(&incoming.message)) {
                send_to(incoming.peer, Echoed{.sent = echo->sent}, Channel::kFast);
                continue;
            }
            if (const auto* echoed = std::get_if<Echoed>(&incoming.message)) {
                // (Seated now: who it was from is who answered, the places being closed up meanwhile or not.)
                if (const int player = player_of(incoming.peer); player >= 0 && player < kMostPlayers) {
                    engine_.at(static_cast<std::size_t>(player)) = static_cast<int>(now_ms() - echoed->sent);
                }
                continue;
            }
        }
        // (Its player as he is now: the places may have been closed up since it came.)
        incoming.player = player_of(incoming.peer);
        return incoming;
    }
    return std::nullopt;
}

void Connection::send_to(int peer, const Message& message, Channel channel) {
    if (channel == Channel::kFast) {
        outgoing_[peer].push_back(message);  // (with the rest of the pass's, at flush)
        return;
    }
    transport_->send(peer, encode(message), channel);
}

void Connection::send(int player, const Message& message, Channel channel) {
    if (const int peer = peer_of(player); peer >= 0) {
        send_to(peer, message, channel);
    }
}

void Connection::send_to_peer(int peer, const Message& message, Channel channel) {
    send_to(peer, message, channel);
}

void Connection::broadcast(const Message& message, Channel channel) {
    if (!hosting()) {
        send_to(0, message, channel);
        return;
    }
    for (std::size_t player = 1; player < peers_.size(); ++player) {
        if (peers_[player] >= 0) {
            send_to(peers_[player], message, channel);
        }
    }
}

void Connection::flush() {
    for (auto& [peer, messages] : outgoing_) {
        if (messages.empty()) {
            continue;
        }
        Bundle bundle;
        bundle.parts.reserve(messages.size());
        for (const Message& message : messages) {
            bundle.parts.push_back(encode(message));
        }
        transport_->send(peer, encode(bundle), Channel::kFast);
        messages.clear();
    }
}

void Connection::drop_peer(int peer) {
    transport_->drop(peer);
}

void Connection::quit() {
    if (!hosting()) {
        send_to(0, Quit{}, Channel::kReliable);
        transport_->drain(kQuitWaitMs);
    }
}

void Connection::echo() {
    if (const std::uint32_t now = now_ms(); !echoed_at_ || now - *echoed_at_ >= kEchoEvery) {
        echoed_at_ = now;
        broadcast(Echo{.sent = now}, Channel::kFast);
    }
}

void Connection::set_name(int player, std::string name) {
    if (player >= 0) {
        names_.resize(std::max(names_.size(), static_cast<std::size_t>(player) + 1));
        names_.at(static_cast<std::size_t>(player)) = std::move(name);
    }
}

int Connection::peer_of(int player) const noexcept {
    if (!hosting()) {
        return player == 0 ? 0 : -1;
    }
    return player >= 0 && std::cmp_less(player, peers_.size()) ? peers_[static_cast<std::size_t>(player)] : -1;
}

int Connection::player_of(int peer) const noexcept {
    if (!hosting()) {
        return 0;
    }
    if (peer < 0) {
        return -1;
    }
    const auto found = std::ranges::find(peers_, peer);
    return found != peers_.end() ? static_cast<int>(found - peers_.begin()) : -1;
}

void Connection::seat(int player, int peer) {
    if (player > 0 && hosting()) {
        peers_.resize(std::max(peers_.size(), static_cast<std::size_t>(player) + 1), -1);
        peers_.at(static_cast<std::size_t>(player)) = peer;
    }
}

void Connection::close_up() {
    std::vector<int> peers{-1};
    std::vector<std::string> names{names_.empty() ? std::string{} : names_.front()};
    std::array<std::optional<int>, kMostPlayers> engine{};
    for (std::size_t player = 1; player < peers_.size(); ++player) {
        if (peers_[player] >= 0) {
            engine.at(peers.size()) = player < engine_.size() ? engine_.at(player) : std::nullopt;
            peers.push_back(peers_[player]);
            names.push_back(player < names_.size() ? names_.at(player) : std::string{});
        }
    }
    peers_ = std::move(peers);
    names_ = std::move(names);
    engine_ = engine;
}

std::optional<int> Connection::network_ms(int player) const {
    if (player <= 0 || player >= kMostPlayers) {
        return std::nullopt;  // (the host: nothing to measure)
    }
    if (hosting()) {
        const int peer = peer_of(player);
        return peer >= 0 ? transport_->round_trip_ms(peer) : std::nullopt;
    }
    const std::uint16_t reported = reported_network_.at(static_cast<std::size_t>(player));
    return reported != kUnknown ? std::optional<int>{reported} : std::nullopt;
}

std::optional<int> Connection::engine_ms(int player) const {
    if (player <= 0 || player >= kMostPlayers) {
        return std::nullopt;
    }
    if (hosting()) {
        return engine_.at(static_cast<std::size_t>(player));
    }
    const std::uint16_t reported = reported_engine_.at(static_cast<std::size_t>(player));
    return reported != kUnknown ? std::optional<int>{reported} : std::nullopt;
}

void Connection::reported(const std::array<std::uint16_t, kMostPlayers>& network,
                          const std::array<std::uint16_t, kMostPlayers>& engine) {
    reported_network_ = network;
    reported_engine_ = engine;
}

void Connection::forget_round_trip(int player) {
    if (player >= 0 && player < kMostPlayers) {
        engine_.at(static_cast<std::size_t>(player)).reset();
    }
}

void Connection::forget_reported() {
    reported_network_.fill(kUnknown);
    reported_engine_.fill(kUnknown);
}

}  // namespace lvr::net
