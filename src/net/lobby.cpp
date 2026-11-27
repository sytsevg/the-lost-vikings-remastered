#include "net/lobby.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include "log/log.hpp"

namespace lvr::net {

namespace {

constexpr std::uint32_t kStatusEvery = 500;  // milliseconds
constexpr std::uint32_t kCountdownMs = 3000;

// The input delay for these round trips: none under 50 ms, a step under 150, else two.
int delay_for(int round_trip_ms) {
    constexpr int kNone = 50;
    constexpr int kOne = 150;
    return round_trip_ms < kNone ? 0 : (round_trip_ms < kOne ? 1 : 2);
}

std::uint16_t as_sent(std::optional<int> ms) {
    return ms ? static_cast<std::uint16_t>(std::clamp(*ms, 0, int{kUnknown} - 1)) : kUnknown;
}

}  // namespace

HostLobby::HostLobby(Connection& connection, int slot, bool cheat)
    : connection_{&connection}, slot_{slot}, cheat_{cheat} {
    Connection& c = connection;
    if (c.names().empty()) {
        c.set_names({shown_name(c.identity().name, 0)});
    }
    // Those still there (back from a game), in their order: each told his place again, and everyone who is there.
    c.close_up();
    for (int player = 1; player < c.seated(); ++player) {
        c.send(player, Welcome{.player = static_cast<std::uint8_t>(player)}, Channel::kReliable);
    }
    if (c.seated() > 1) {
        send_status();
    }
}

std::expected<std::optional<Started>, std::string> HostLobby::poll() {
    Connection& c = *connection_;
    c.receive();
    while (const std::optional<Incoming> incoming = c.next()) {
        if (incoming->kind == Incoming::Kind::kLeft) {
            if (incoming->player > 0) {  // the ones after him move up, each told his place again
                c.unseat(incoming->player);
                c.close_up();
                for (int player = 1; player < c.seated(); ++player) {
                    c.send(player, Welcome{.player = static_cast<std::uint8_t>(player)}, Channel::kReliable);
                }
                send_status();
            }
            continue;
        }
        if (const auto* hello = std::get_if<Hello>(&incoming->message);
            hello != nullptr && incoming->kind == Incoming::Kind::kMessage && incoming->player < 0) {
            welcome(incoming->peer, *hello);
        }
        // (Anything else is late from a game before.)
    }
    if (const std::uint32_t now = c.now_ms(); !status_sent_at_ || now - *status_sent_at_ >= kStatusEvery) {
        status_sent_at_ = now;  // now and then, to everyone
        send_status();
    }
    c.echo();
    if (counting_until_) {  // each second of the countdown told; at its end, the start
        if (const int left = countdown(); left != countdown_sent_) {
            countdown_sent_ = left;
            send_status();
        }
        if (countdown() == 0) {
            counting_until_.reset();
            start_wanted_ = true;
        }
    }
    if (!start_wanted_) {
        c.flush();
        return std::nullopt;
    }
    // The delay from the longest round trip, as far as known.
    int longest = 0;
    for (int player = 1; player < c.seated(); ++player) {
        longest = std::max(longest, c.link_ms(c.peer_of(player)).value_or(0));
    }
    start_wanted_ = false;
    countdown_sent_ = 0;
    c.set_game(static_cast<std::uint16_t>(c.game() + 1));
    Started started{.player = 0,
                    .players = static_cast<int>(c.names().size()),
                    .delay = delay_for(longest),
                    .slot = slot_,
                    .cheat = cheat_,
                    .names = c.names(),
                    .state = {},
                    .first_step = 0,
                    .arrives = 0,
                    .presence = {}};
    c.broadcast(Start{.game = c.game(),
                      .players = static_cast<std::uint8_t>(started.players),
                      .delay = static_cast<std::uint8_t>(started.delay),
                      .slot = static_cast<std::uint16_t>(slot_),
                      .cheat = cheat_,
                      .names = started.names},
                Channel::kReliable);
    c.flush();
    log::info("game {}: starts for {} player(s), {} step(s) of input delay for the longest round trip, {} ms", c.game(),
              started.players, started.delay, longest);
    return started;
}

int HostLobby::slot() const noexcept {
    return slot_;
}

int HostLobby::countdown() const {
    if (!counting_until_) {
        return 0;
    }
    constexpr int kSecond = 1000;
    const auto left = static_cast<std::int32_t>(*counting_until_ - connection_->now_ms());
    return left <= 0 ? 0 : static_cast<int>((left + kSecond - 1) / kSecond);
}

void HostLobby::set_slot(int slot) {
    slot_ = slot;
}

void HostLobby::start() {
    if (!counting_until_) {
        counting_until_ = connection_->now_ms() + kCountdownMs;
        countdown_sent_ = countdown();
        send_status();
    }
}

void HostLobby::cancel_start() {
    if (counting_until_) {
        counting_until_.reset();
        countdown_sent_ = 0;
        send_status();
    }
}

// One says hello: a place, or turned away with why.
void HostLobby::welcome(int peer, const Hello& hello) {
    Connection& c = *connection_;
    std::optional<std::string> refused = c.refusal(hello);
    if (!refused && std::cmp_greater_equal(c.names().size(), kMostPlayers)) {
        refused = "the game is full";
    }
    if (refused) {
        log::info("a hello turned away: {}", *refused);
        c.send_to_peer(peer, Refused{.why = *refused}, Channel::kReliable);
        c.drop_peer(peer);
        return;
    }
    const int player = static_cast<int>(c.names().size());
    c.seat(player, peer);
    c.set_name(player, shown_name(hello.name, player));
    log::info("player {} ({}) has a place in the lobby", player + 1, c.names().at(static_cast<std::size_t>(player)));
    c.send(player, Welcome{.player = static_cast<std::uint8_t>(player)}, Channel::kReliable);
    send_status();
}

// Who is there, their round trips and the start, to everyone.
void HostLobby::send_status() {
    Connection& c = *connection_;
    LobbyStatus status{.names = c.names(),
                       .network = {},
                       .engine = {},
                       .slot = static_cast<std::uint16_t>(slot_),
                       .countdown = static_cast<std::uint8_t>(countdown())};
    for (int player = 0; player < kMostPlayers; ++player) {
        status.network.at(static_cast<std::size_t>(player)) = as_sent(c.network_ms(player));
        status.engine.at(static_cast<std::size_t>(player)) = as_sent(c.engine_ms(player));
    }
    c.broadcast(status, Channel::kReliable);
}

GuestLobby::GuestLobby(Connection& connection, bool back, int slot)
    : connection_{&connection}, slot_{slot}, welcomed_{back} {
    if (!back) {
        connection.send(0, connection.hello(), Channel::kReliable);
    }
    connection.forget_reported();
}

std::expected<std::optional<Started>, std::string> GuestLobby::poll() {
    Connection& c = *connection_;
    c.receive();
    while (const std::optional<Incoming> incoming = c.next()) {
        if (incoming->kind == Incoming::Kind::kLeft) {
            return std::unexpected{"the host is gone"};
        }
        if (incoming->kind != Incoming::Kind::kMessage) {
            continue;
        }
        const Message& message = incoming->message;
        if (const auto* welcome = std::get_if<Welcome>(&message)) {
            c.set_player(welcome->player);
            welcomed_ = true;
        } else if (const auto* status = std::get_if<LobbyStatus>(&message)) {
            c.set_names(status->names);
            c.reported(status->network, status->engine);
            slot_ = status->slot;
            countdown_ = status->countdown;
        } else if (const auto* refused = std::get_if<Refused>(&message)) {
            return std::unexpected{std::format("refused: {}", refused->why)};
        } else if (const auto* joining = std::get_if<Joining>(&message)) {  // a game under way
            c.set_player(joining->player);
            c.set_names(joining->names);
            c.set_game(joining->game);
            c.flush();
            return Started{.player = joining->player,
                           .players = kMostPlayers,
                           .delay = joining->delay,
                           .slot = 0,       // (the state has it)
                           .cheat = false,  // (the state has it)
                           .names = joining->names,
                           .state = joining->state,
                           .first_step = joining->step,
                           .arrives = joining->arrives,
                           .presence = joining->presence};
        } else if (const auto* start = std::get_if<Start>(&message)) {
            c.set_names(start->names);
            c.set_game(start->game);
            c.flush();
            return Started{.player = c.player(),
                           .players = start->players,
                           .delay = start->delay,
                           .slot = start->slot,
                           .cheat = start->cheat,
                           .names = start->names,
                           .state = {},
                           .first_step = 0,
                           .arrives = 0,
                           .presence = {}};
        }
    }
    c.flush();
    return std::nullopt;
}

bool GuestLobby::welcomed() const noexcept {
    return welcomed_;
}

int GuestLobby::slot() const noexcept {
    return slot_;
}

int GuestLobby::countdown() const {
    return countdown_;
}

}  // namespace lvr::net
