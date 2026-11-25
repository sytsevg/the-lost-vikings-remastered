#include "lvr/online.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <utility>

#include "lvr/archive.hpp"

namespace lvr {

namespace {

constexpr int kWindow = 8;                  // the steps rollback can run ahead (ADR-0012)
constexpr std::uint32_t kEchoEvery = 500;   // milliseconds
constexpr std::uint32_t kLobbyEvery = 500;  // milliseconds

// Milliseconds on a clock of this machine's own (only differences are sent back to it).
std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::uint16_t as_sent(std::optional<int> ms) {
    return ms ? static_cast<std::uint16_t>(std::clamp(*ms, 0, int{net::kUnknown} - 1)) : net::kUnknown;
}
constexpr double kStepMilliseconds = 1000.0 * 3.0 / kDisplayHz;

// The input delay for these round trips: none under 50 ms, a step under 150, else two.
int delay_for(int round_trip_ms) {
    constexpr int kNone = 50;
    constexpr int kOne = 150;
    return round_trip_ms < kNone ? 0 : (round_trip_ms < kOne ? 1 : 2);
}

// The changes of who is there from a step on, as the protocol sends them.
std::vector<net::Presence> presence_from(const net::Rollback& rollback, std::uint32_t from) {
    const std::vector<net::PresenceChange> scheduled = rollback.presence_from(from);
    std::vector<net::Presence> changes;
    changes.reserve(scheduled.size());
    for (const net::PresenceChange& change : scheduled) {
        changes.push_back({.player = static_cast<std::uint8_t>(change.player), .at = change.at, .there = change.there});
    }
    return changes;
}

}  // namespace

NetGame::NetGame(std::unique_ptr<net::Transport> transport, Identity identity)
    : transport_{std::move(transport)}, identity_{std::move(identity)} {
    engine_.assign(net::kMostPlayers, std::nullopt);
}

std::expected<std::unique_ptr<NetGame>, std::string> NetGame::host(std::uint16_t port, int players, int slot,
                                                                   bool cheat, std::optional<int> delay,
                                                                   Identity identity) {
    auto transport = net::Transport::host(port);
    if (!transport) {
        return std::unexpected{transport.error()};
    }
    std::unique_ptr<NetGame> game{new NetGame{std::move(*transport), std::move(identity)}};
    game->auto_start_ = players > 0;
    game->wanted_ = players > 0 ? std::clamp(players, 1, net::kMostPlayers) : net::kMostPlayers;
    game->slot_ = slot;
    game->cheat_ = cheat;
    game->delay_ = delay;
    game->names_ = {net::shown_name(game->identity_.name, 0)};
    game->peers_ = {-1};
    return game;
}

std::expected<std::unique_ptr<NetGame>, std::string> NetGame::join(const std::string& address, std::uint16_t port,
                                                                   Identity identity) {
    auto transport = net::Transport::join(address, port);
    if (!transport) {
        return std::unexpected{transport.error()};
    }
    std::unique_ptr<NetGame> game{new NetGame{std::move(*transport), std::move(identity)}};
    game->send(0,
               net::Hello{.protocol = net::kProtocol,
                          .state_version = kStateVersion,
                          .build = game->identity_.build,
                          .game = game->identity_.game,
                          .name = game->identity_.name},
               net::Channel::kReliable);
    return game;
}

void NetGame::send(int peer, const net::Message& message, net::Channel channel) {
    transport_->send(peer, net::encode(message), channel);
}

void NetGame::broadcast(const net::Message& message, net::Channel channel) {
    const std::vector<std::uint8_t> bytes = net::encode(message);
    if (!hosting()) {
        transport_->send(0, bytes, channel);
        return;
    }
    for (std::size_t player = 1; player < peers_.size(); ++player) {
        if (peers_[player] >= 0 && (gone_.empty() || !gone_.at(player))) {
            transport_->send(peers_[player], bytes, channel);
        }
    }
}

int NetGame::peer_of(int player) const noexcept {
    if (!hosting()) {
        return player == 0 ? 0 : -1;
    }
    return player >= 0 && std::cmp_less(player, peers_.size()) ? peers_[static_cast<std::size_t>(player)] : -1;
}

int NetGame::player_of(int peer) const noexcept {
    if (!hosting()) {
        return 0;
    }
    const auto found = std::ranges::find(peers_, peer);
    return found != peers_.end() ? static_cast<int>(found - peers_.begin()) : -1;
}

std::expected<std::optional<NetGame::Joined>, std::string> NetGame::lobby() {
    std::vector<net::Event> events = std::exchange(pending_, {});  // (what came with the way back here first)
    for (net::Event& event : transport_->poll()) {
        events.push_back(std::move(event));
    }
    for (std::size_t i = 0; i < events.size(); ++i) {
        auto taken = lobby_event(events[i]);
        if (!taken || *taken) {
            if (taken && *taken) {
                pending_.assign(events.begin() + static_cast<std::ptrdiff_t>(i) + 1, events.end());  // (the game's)
            }
            return taken;
        }
    }
    if (!hosting()) {
        return std::nullopt;
    }
    if (const std::uint32_t now = now_ms(); now - lobby_sent_at_ >= kLobbyEvery) {  // now and then, to everyone
        lobby_sent_at_ = now;
        send_lobby();
        broadcast(net::Echo{.sent = now}, net::Channel::kFast);
    }
    if (counting_until_) {  // each second of the countdown told; at its end, the start
        if (const int left = countdown(); left != countdown_sent_) {
            countdown_sent_ = left;
            send_lobby();
        }
        if (countdown() == 0) {
            counting_until_.reset();
            start_wanted_ = true;
        }
    }
    if (auto_start_ ? std::cmp_less(names_.size(), wanted_) : !start_wanted_) {
        return std::nullopt;
    }
    // The delay from the longest round trip: once each has one (started by the host's word: as far as known).
    int longest = 0;
    for (std::size_t player = 1; player < peers_.size(); ++player) {
        const std::optional<int> round_trip = transport_->round_trip_ms(peers_[player]);
        if (!round_trip && !delay_ && auto_start_) {
            return std::nullopt;
        }
        longest = std::max(longest, round_trip.value_or(0));
    }
    start_wanted_ = false;
    countdown_sent_ = 0;
    ++game_number_;
    const int delay = delay_.value_or(delay_for(longest));
    broadcast(net::Start{.game = game_number_,
                         .players = static_cast<std::uint8_t>(names_.size()),
                         .delay = static_cast<std::uint8_t>(delay),
                         .slot = static_cast<std::uint16_t>(slot_),
                         .cheat = cheat_,
                         .names = names_},
              net::Channel::kReliable);
    joined_ = Joined{.player = 0,
                     .players = static_cast<int>(names_.size()),
                     .delay = delay,
                     .slot = slot_,
                     .cheat = cheat_,
                     .names = names_,
                     .state = {},
                     .first_step = 0,
                     .arrives = 0,
                     .presence = {}};
    return joined_;
}

std::expected<std::optional<NetGame::Joined>, std::string> NetGame::lobby_event(const net::Event& event) {
    if (event.kind == net::Event::Kind::kLeft) {
        if (!hosting()) {
            return std::unexpected{"the host is gone"};
        }
        if (const int left = player_of(event.peer); left > 0) {  // the ones after him move up
            const auto at = static_cast<std::ptrdiff_t>(left);
            peers_.erase(peers_.begin() + at);
            names_.erase(names_.begin() + at);
            engine_.erase(engine_.begin() + at);
            engine_.emplace_back();
            for (std::size_t player = 1; player < peers_.size(); ++player) {
                send(peers_[player], net::Welcome{.player = static_cast<std::uint8_t>(player)},
                     net::Channel::kReliable);
            }
            send_lobby();
        }
        return std::nullopt;
    }
    if (event.kind != net::Event::Kind::kMessage) {
        return std::nullopt;
    }
    const std::optional<net::Message> message = net::decode(event.bytes);
    if (!message) {
        return std::nullopt;
    }
    if (const auto* echo = std::get_if<net::Echo>(&*message)) {  // the game's round trip, in the lobby too
        send(event.peer, net::Echoed{.sent = echo->sent}, net::Channel::kFast);
        return std::nullopt;
    }
    if (hosting()) {
        if (const auto* echoed = std::get_if<net::Echoed>(&*message)) {
            if (const int player = player_of(event.peer);
                player > 0 && echoed->sent != echo_taken_.at(static_cast<std::size_t>(player))) {
                echo_taken_.at(static_cast<std::size_t>(player)) = echoed->sent;  // (the first of its copies)
                engine_.at(static_cast<std::size_t>(player)) = static_cast<int>(now_ms() - echoed->sent);
            }
            return std::nullopt;
        }
        const auto* hello = std::get_if<net::Hello>(&*message);
        if (hello == nullptr || player_of(event.peer) >= 0) {
            return std::nullopt;
        }
        std::string refused;
        if (hello->protocol != net::kProtocol || hello->state_version != kStateVersion) {
            refused = "another version of the game";
        } else if (hello->build != identity_.build) {
            refused = std::format("another build ({}; the host's is {})", hello->build, identity_.build);
        } else if (hello->game != identity_.game) {
            refused = "other game files";
        } else if (std::cmp_greater_equal(names_.size(), wanted_)) {
            refused = "the game is full";
        }
        if (!refused.empty()) {
            send(event.peer, net::Refused{.why = refused}, net::Channel::kReliable);
            transport_->drop(event.peer);
            return std::nullopt;
        }
        const auto player = static_cast<std::uint8_t>(names_.size());
        peers_.push_back(event.peer);
        names_.push_back(net::shown_name(hello->name, player));
        send(event.peer, net::Welcome{.player = player}, net::Channel::kReliable);
        send_lobby();
        return std::nullopt;
    }
    if (const auto* welcome = std::get_if<net::Welcome>(&*message)) {
        player_ = welcome->player;
        welcomed_ = true;
    } else if (const auto* lobby = std::get_if<net::Lobby>(&*message)) {
        names_ = lobby->names;
        host_network_ = lobby->network;
        host_engine_ = lobby->engine;
        slot_ = lobby->slot;
        countdown_ = lobby->countdown;
    } else if (const auto* refused = std::get_if<net::Refused>(&*message)) {
        return std::unexpected{std::format("refused: {}", refused->why)};
    } else if (const auto* joining = std::get_if<net::Joining>(&*message)) {  // a game under way
        player_ = joining->player;
        names_ = joining->names;
        game_number_ = joining->game;
        joined_ = Joined{.player = joining->player,
                         .players = net::kMostPlayers,
                         .delay = joining->delay,
                         .slot = Session::scene_of(joining->state).value_or(0),
                         .cheat = false,  // (the state has it)
                         .names = joining->names,
                         .state = joining->state,
                         .first_step = joining->step,
                         .arrives = joining->arrives,
                         .presence = joining->presence};
        return joined_;
    } else if (const auto* start = std::get_if<net::Start>(&*message)) {
        names_ = start->names;
        game_number_ = start->game;
        joined_ = Joined{.player = player_,
                         .players = start->players,
                         .delay = start->delay,
                         .slot = start->slot,
                         .cheat = start->cheat,
                         .names = start->names,
                         .state = {},
                         .first_step = 0,
                         .arrives = 0,
                         .presence = {}};
        return joined_;
    }
    return std::nullopt;
}

void NetGame::start(bool at_once) {
    if (!hosting()) {
        return;
    }
    constexpr std::uint32_t kCountdownMs = 3000;
    if (at_once) {
        start_wanted_ = true;
    } else if (!counting_until_) {
        counting_until_ = now_ms() + kCountdownMs;
        countdown_sent_ = countdown();
        send_lobby();
    }
}

void NetGame::cancel_start() {
    if (hosting() && counting_until_) {
        counting_until_.reset();
        countdown_sent_ = 0;
        send_lobby();
    }
}

int NetGame::countdown() const {
    if (!hosting()) {
        return countdown_;
    }
    if (!counting_until_) {
        return 0;
    }
    constexpr int kSecond = 1000;
    const auto left = static_cast<std::int32_t>(*counting_until_ - now_ms());
    return left <= 0 ? 0 : static_cast<int>((left + kSecond - 1) / kSecond);
}

void NetGame::send_lobby() {
    net::Lobby message{.names = names_,
                       .network = {},
                       .engine = {},
                       .slot = static_cast<std::uint16_t>(slot_),
                       .countdown = static_cast<std::uint8_t>(countdown())};
    for (int player = 0; player < net::kMostPlayers; ++player) {
        message.network.at(static_cast<std::size_t>(player)) = as_sent(network_ms(player));
        message.engine.at(static_cast<std::size_t>(player)) = as_sent(engine_ms(player));
    }
    broadcast(message, net::Channel::kReliable);
}

void NetGame::back_to_lobby() {
    if (hosting()) {
        broadcast(net::Back{}, net::Channel::kReliable);
        to_lobby();
    }
}

void NetGame::to_lobby() {
    game_.reset();
    rollback_.reset();
    session_ = nullptr;
    acked_.clear();
    hashes_.clear();
    logged_ = false;
    waited_ = 0;
    back_ = false;
    joined_.reset();
    countdown_ = 0;
    catch_up_to_ = 0;
    host_network_.fill(net::kUnknown);
    host_engine_.fill(net::kUnknown);
    if (hosting()) {
        // Those still there, in their order: each told his number again, and everyone who is there.
        std::vector<int> peers{-1};
        std::vector<std::string> names{names_.empty() ? net::shown_name(identity_.name, 0) : names_.front()};
        std::vector<std::optional<int>> engine{std::nullopt};
        for (std::size_t player = 1; player < peers_.size(); ++player) {
            if (peers_[player] >= 0 && (gone_.size() <= player || !gone_.at(player))) {
                peers.push_back(peers_[player]);
                names.push_back(player < names_.size() ? names_.at(player) : std::string{});
                engine.push_back(player < engine_.size() ? engine_.at(player) : std::nullopt);
            }
        }
        peers_ = std::move(peers);
        names_ = std::move(names);
        engine_ = std::move(engine);
        engine_.resize(net::kMostPlayers);
        for (std::size_t player = 1; player < peers_.size(); ++player) {
            send(peers_[player], net::Welcome{.player = static_cast<std::uint8_t>(player)}, net::Channel::kReliable);
        }
        send_lobby();
    }
    gone_.clear();
}

void NetGame::begin(Session& session) {
    const Joined joined = joined_.value_or(Joined{});  // (the lobby's, once it started)
    session_ = &session;
    delay_steps_ = joined.delay;
    // A place for every player there can be (the world has them too): those not there absent, until someone comes.
    const net::Settings settings{.players = net::kMostPlayers,
                                 .local = joined.player,
                                 .delay = joined.delay,
                                 .window = kWindow,
                                 .hash_every = 8,
                                 .first_step = joined.first_step,
                                 .decides_presence = hosting()};  // (the host's word on who is there, for a client)
    game_ = std::make_unique<CoopGame>(session, static_cast<std::size_t>(kWindow) + 2);
    rollback_ = std::make_unique<net::Rollback>(*game_, settings);
    acked_.assign(net::kMostPlayers, {joined.first_step, joined.first_step, joined.first_step});
    gone_.assign(net::kMostPlayers, false);
    engine_.assign(net::kMostPlayers, std::nullopt);
    names_.resize(net::kMostPlayers);
    if (hosting()) {
        peers_.resize(net::kMostPlayers, -1);
    }
    hash_sent_ = joined.first_step + ((7 + 8 - (joined.first_step % 8)) % 8);  // (every 8th: 7, 15, ...)
    if (joined.state.empty()) {  // from the lobby: the places nobody took, away from the start
        for (int player = joined.players; player < net::kMostPlayers; ++player) {
            rollback_->leave(player, 0);
            gone_.at(static_cast<std::size_t>(player)) = true;
        }
    } else {  // under way: who is away in the state given, and this one until he arrives
        for (int player = 0; player < net::kMostPlayers; ++player) {
            if (!session.world().players.at(static_cast<std::size_t>(player)).here) {
                rollback_->leave(player, joined.first_step);
                gone_.at(static_cast<std::size_t>(player)) = player != player_;
            }
        }
        for (const net::Presence& change : joined.presence) {  // the others' comings and goings to come
            if (change.player < net::kMostPlayers && std::cmp_not_equal(change.player, player_)) {
                (change.there ? rollback_->arrive(change.player, change.at)
                              : rollback_->leave(change.player, change.at));
                gone_.at(change.player) = !change.there;
            }
        }
        rollback_->arrive(player_, joined.arrives);
        catch_up_to_ = joined.arrives;
    }
    decided_sent_ = joined.first_step;
    session.set_player_names(names_);
}

void NetGame::welcome_under_way(int peer, const net::Hello& hello) {
    std::string refused;
    if (hello.protocol != net::kProtocol || hello.state_version != kStateVersion) {
        refused = "another version of the game";
    } else if (hello.build != identity_.build) {
        refused = std::format("another build ({}; the host's is {})", hello.build, identity_.build);
    } else if (hello.game != identity_.game) {
        refused = "other game files";
    }
    int place = -1;
    for (int player = 1; player < net::kMostPlayers && place < 0 && refused.empty(); ++player) {
        if (gone_.at(static_cast<std::size_t>(player)) && peer_of(player) < 0) {
            place = player;
        }
    }
    if (refused.empty() && place < 0) {
        refused = "the game is full";
    }
    if (!refused.empty()) {
        send(peer, net::Refused{.why = refused}, net::Channel::kReliable);
        transport_->drop(peer);
        return;
    }
    const auto at = static_cast<std::size_t>(place);
    rollback_->catch_up();  // (a correction still to run: the states kept are right again)
    // The state before the last step every input is known for (all machines have it so), and his first step: past
    // any the host's input is known for, so no machine has confirmed it.
    const std::uint32_t from = std::min(rollback_->confirmed(), rollback_->step());
    // (Far enough on for him to load the level meanwhile, so the others don't wait for him: about 0.8 s.)
    constexpr std::uint32_t kJoinLead = 16;
    const std::uint32_t arrives = std::max(rollback_->step(), rollback_->local_known()) + kJoinLead;
    peers_.at(at) = peer;
    names_.at(at) = net::shown_name(hello.name, place);
    gone_.at(at) = false;
    acked_.at(at) = {from, from, from};
    engine_.at(at) = std::nullopt;
    send(peer,
         net::Joining{.game = game_number_,
                      .player = static_cast<std::uint8_t>(place),
                      .step = from,
                      .arrives = arrives,
                      .delay = static_cast<std::uint8_t>(delay_steps_),
                      .names = names_,
                      .state = Session::state_bytes(game_->snapshot_before(from, rollback_->step())),
                      .presence = presence_from(*rollback_, from)},
         net::Channel::kReliable);
    rollback_->arrive(place, arrives);
    broadcast(net::Arrive{.player = static_cast<std::uint8_t>(place), .at = arrives, .name = names_.at(at)},
              net::Channel::kReliable);
    if (session_ != nullptr) {
        session_->set_player_names(names_);
    }
}

void NetGame::leaves(int player) {
    if (player <= 0 || gone_.at(static_cast<std::size_t>(player))) {
        return;
    }
    const std::uint32_t from = rollback_->known(player);  // what he gave counts; after that, nothing
    rollback_->leave(player, from);
    gone_.at(static_cast<std::size_t>(player)) = true;
    peers_.at(static_cast<std::size_t>(player)) = -1;  // (the place is free to come into again)
    broadcast(net::Leave{.player = static_cast<std::uint8_t>(player), .from = from}, net::Channel::kReliable);
}

std::expected<void, std::string> NetGame::take(int peer, const net::Message& message) {
    const int sender = player_of(peer);
    if (const auto* hello = std::get_if<net::Hello>(&message)) {
        if (hosting() && sender < 0) {
            welcome_under_way(peer, *hello);
        }
    } else if (const auto* arrive = std::get_if<net::Arrive>(&message)) {
        if (!hosting() && arrive->player < net::kMostPlayers) {
            rollback_->arrive(arrive->player, arrive->at);
            names_.at(arrive->player) = arrive->name;
            gone_.at(arrive->player) = false;
            if (session_ != nullptr) {
                session_->set_player_names(names_);
            }
        }
    } else if (const auto* inputs = std::get_if<net::Inputs>(&message)) {
        // A player's inputs: on the host from his own connection only (a welcomed one); on a client from the host,
        // who passes everyone's on. And of this game, not one before.
        const bool from_him = hosting() ? sender > 0 && std::cmp_equal(inputs->player, sender) : true;
        if (from_him && std::cmp_not_equal(inputs->player, player_) && inputs->game == game_number_) {
            for (std::size_t i = 0; i < inputs->inputs.size(); ++i) {
                rollback_->add_remote(inputs->player, inputs->first + static_cast<std::uint32_t>(i), inputs->inputs[i]);
            }
        }
    } else if (const auto* status = std::get_if<net::Status>(&message)) {
        // (Its copy the other way, the same bytes, later: not taken again, as if it said where he is now.)
        const bool again = sender >= 0 && std::cmp_less(sender, last_status_.size()) &&
                           last_status_.at(static_cast<std::size_t>(sender)) == *status;
        if (!again && sender >= 0 && std::cmp_less(sender, last_status_.size())) {
            last_status_.at(static_cast<std::size_t>(sender)) = *status;
        }
        if (!again && sender >= 0 && std::cmp_less(sender, acked_.size()) && status->game == game_number_) {
            auto& acked = acked_.at(static_cast<std::size_t>(sender));
            for (std::size_t player = 0; player < acked.size(); ++player) {
                // (Known for ever: that player away; it says nothing of what he gives once he comes.)
                if (status->knows.at(player) != net::kAll) {
                    acked.at(player) = std::max(acked.at(player), status->knows.at(player));
                }
            }
            // Where he is now: where he said, and half the round trip on.
            const int one_way = transport_->round_trip_ms(peer).value_or(0) / 2;
            const auto later = static_cast<std::uint32_t>(std::lround(one_way / kStepMilliseconds));
            rollback_->report(sender, status->step + later, status->advantage);
            if (!hosting()) {  // the host's round trips, to the other clients too
                host_network_ = status->network;
                host_engine_ = status->engine;
            }
        }
    } else if (const auto* echo = std::get_if<net::Echo>(&message)) {
        send(peer, net::Echoed{.sent = echo->sent}, net::Channel::kFast);
    } else if (const auto* echoed = std::get_if<net::Echoed>(&message)) {
        // (The first of its copies: the later one isn't the round trip.)
        if (sender >= 0 && std::cmp_less(sender, engine_.size()) &&
            echoed->sent != echo_taken_.at(static_cast<std::size_t>(sender))) {
            echo_taken_.at(static_cast<std::size_t>(sender)) = echoed->sent;
            engine_.at(static_cast<std::size_t>(sender)) = static_cast<int>(now_ms() - echoed->sent);
        }
    } else if (const auto* decided = std::get_if<net::Decided>(&message)) {
        if (!hosting() && decided->game == game_number_) {
            std::vector<net::PresenceChange> changes;
            changes.reserve(decided->presence.size());
            for (const net::Presence& change : decided->presence) {
                changes.push_back({.player = change.player, .at = change.at, .there = change.there});
            }
            rollback_->know_presence(changes);  // (before the bound: the changes it covers known with it)
            rollback_->decided_until(decided->until);
        }
    } else if (const auto* hash = std::get_if<net::Hash>(&message)) {
        if (hash->game == game_number_ && (!hosting() || (sender > 0 && std::cmp_equal(hash->player, sender)))) {
            rollback_->add_remote_hash(hash->player, hash->step, hash->hash);
        }
    } else if (const auto* leave = std::get_if<net::Leave>(&message)) {
        if (!hosting() && std::cmp_not_equal(leave->player, player_) && std::cmp_less(leave->player, gone_.size())) {
            rollback_->leave(leave->player, leave->from);
            gone_.at(leave->player) = true;
        }
    } else if (std::holds_alternative<net::Quit>(message)) {
        if (hosting()) {
            leaves(sender);
            transport_->drop(peer);
        }
    }
    return {};
}

std::expected<void, std::string> NetGame::poll() {
    if (back_) {
        return {};  // (going back to the lobby: what comes is the lobby's)
    }
    std::vector<net::Event> events = std::exchange(pending_, {});  // (what came with the Start first)
    for (net::Event& event : transport_->poll()) {
        events.push_back(std::move(event));
    }
    for (std::size_t i = 0; i < events.size(); ++i) {
        const net::Event& event = events[i];
        if (event.kind == net::Event::Kind::kLeft) {
            if (!hosting()) {
                return std::unexpected{"the host left the game"};
            }
            leaves(player_of(event.peer));
            continue;
        }
        if (event.kind != net::Event::Kind::kMessage) {
            continue;
        }
        if (const std::optional<net::Message> message = net::decode(event.bytes)) {
            if (std::holds_alternative<net::Back>(*message) && !hosting()) {  // the game is over: the lobby again
                back_ = true;
                pending_.assign(events.begin() + static_cast<std::ptrdiff_t>(i) + 1, events.end());
                return {};
            }
            if (auto taken = take(event.peer, *message); !taken) {
                return taken;
            }
        }
    }
    return flush();
}

std::expected<void, std::string> NetGame::flush() {
    const int players = static_cast<int>(acked_.size());
    // To each peer: every player's inputs he hasn't acknowledged (a client gives the host his own; the host passes
    // on everyone's but the client's own), and where this machine is.
    for (int to = 0; to < players; ++to) {
        const int peer = peer_of(to);
        if (to == player_ || peer < 0 || gone_.at(static_cast<std::size_t>(to))) {
            continue;
        }
        for (int whose = 0; whose < players; ++whose) {
            if (whose == to || (!hosting() && whose != player_)) {
                continue;
            }
            const std::uint32_t from = acked_.at(static_cast<std::size_t>(to)).at(static_cast<std::size_t>(whose));
            const auto unsent = rollback_->known_from(whose, from);
            if (unsent.empty()) {
                continue;
            }
            net::Inputs message{.game = game_number_,
                                .player = static_cast<std::uint8_t>(whose),
                                .first = unsent.front().first,
                                .inputs = {}};
            for (const auto& [step, input] : unsent) {
                message.inputs.push_back(input);
            }
            send(peer, message, net::Channel::kFast);
        }
        net::Status status{.game = game_number_,
                           .player = static_cast<std::uint8_t>(player_),
                           .step = rollback_->step(),
                           .advantage = static_cast<std::int16_t>(rollback_->advantage(to)),
                           .knows = {}};
        for (int player = 0; player < players; ++player) {
            status.knows.at(static_cast<std::size_t>(player)) = rollback_->known(player);
            if (hosting()) {
                status.network.at(static_cast<std::size_t>(player)) = as_sent(network_ms(player));
                status.engine.at(static_cast<std::size_t>(player)) = as_sent(engine_ms(player));
            }
        }
        send(peer, status, net::Channel::kFast);
    }
    // Who is there is decided before the host's own next step (an arrival is always after it): said the reliable way,
    // so after any Arrive or Leave.
    if (hosting() && rollback_->local_known() != decided_sent_) {
        constexpr std::uint32_t kRecent = 256;  // the steps whose comings and goings go with it (far more than late)
        decided_sent_ = rollback_->local_known();
        broadcast(net::Decided{.game = game_number_,
                               .until = decided_sent_,
                               .presence = presence_from(*rollback_, decided_sent_ - std::min(decided_sent_, kRecent))},
                  net::Channel::kFast);
    }
    if (const std::uint32_t now = now_ms(); now - echoed_at_ >= kEchoEvery) {  // the game's round trips
        echoed_at_ = now;
        broadcast(net::Echo{.sent = now}, net::Channel::kFast);
    }
    while (const std::optional<std::uint64_t> hash = rollback_->hash_at(hash_sent_)) {
        broadcast(
            net::Hash{
                .game = game_number_, .player = static_cast<std::uint8_t>(player_), .step = hash_sent_, .hash = *hash},
            net::Channel::kReliable);
        hashes_.emplace_back(hash_sent_, *hash);
        hash_sent_ += 8;
    }
    if (const std::optional<std::uint32_t> out = rollback_->out_of_step()) {
        std::string written;
        if (!logged_ && !log_folder_.empty()) {  // what went before, for lv-replay --coop
            logged_ = true;
            std::error_code ignored;
            std::filesystem::create_directories(log_folder_, ignored);
            const std::filesystem::path file =
                log_folder_ / std::format("coop-{}-player{}.log", identity_.build, player_ + 1);
            std::ofstream out_file{file};
            out_file << log();
            written = out_file ? std::format(" (the game's log: {})", file.string()) : std::string{};
        }
        return std::unexpected{std::format("out of step with another machine at step {}{}", *out, written)};
    }
    return {};
}

void NetGame::quit() {
    if (!hosting() && transport_) {
        send(0, net::Quit{}, net::Channel::kReliable);
        transport_->drain(500);
    }
}

std::string NetGame::log() const {
    const Joined joined = joined_.value_or(Joined{});
    // (One who joined under way has the steps from the state he was given only: his log says so, and only the
    // host's, from step 0, can be played again.)
    std::string text =
        std::format("coop 1\nbuild {}\nslot {}\nplayers {}\ncheat {}\nplayer {}\nfirst {}\n", identity_.build,
                    joined.slot, joined.players, joined.cheat ? 1 : 0, joined.player, joined.first_step);
    const std::uint32_t confirmed = rollback_ ? std::min(rollback_->confirmed(), rollback_->step()) : 0;
    for (std::uint32_t step = joined.first_step; rollback_ && step < confirmed; ++step) {
        const std::optional<std::vector<net::Input>> inputs = rollback_->inputs_at(step);
        if (!inputs) {
            break;
        }
        text += std::format("step {}", step);
        for (const net::Input& input : *inputs) {
            text += ' ';
            for (const std::uint8_t byte : input.bytes) {
                text += std::format("{:02x}", byte);
            }
        }
        text += '\n';
    }
    for (const auto& [step, hash] : hashes_) {
        text += std::format("hash {} {:016x}\n", step, hash);
    }
    return text;
}

bool NetGame::wants_input() const {
    return rollback_->local_known() <= rollback_->step() + static_cast<std::uint32_t>(delay_steps_);
}

void NetGame::give(const StepInput& input) {
    (void)rollback_->add_local(pack(input, player_));
}

std::optional<std::vector<Screen>> NetGame::step() {
    if (rollback_->step() < catch_up_to_) {  // joined under way: up to his arrival at once, unseen, then fading in
        while (rollback_->step() < catch_up_to_ && rollback_->advance(false)) {
        }
        session_->drop_carried_effects();
        if (rollback_->step() < catch_up_to_) {
            return std::nullopt;  // (the others' inputs for the rest still on their way)
        }
        session_->fade_in();
    }
    if (rollback_->ahead()) {
        rollback_->catch_up();  // a refresh to let the others catch up
        return std::nullopt;
    }
    if (!rollback_->advance()) {
        ++waited_;
        return std::nullopt;
    }
    waited_ = 0;
    return game_->take_pictures();
}

std::optional<std::string> NetGame::waiting_for() const {
    if (waited_ == 0 || !rollback_) {
        return std::nullopt;
    }
    int slowest = -1;
    std::uint32_t least = 0;
    for (int player = 0; std::cmp_less(player, acked_.size()); ++player) {
        if (player == player_ || gone_.at(static_cast<std::size_t>(player))) {
            continue;
        }
        if (const std::uint32_t known = rollback_->known(player); slowest < 0 || known < least) {
            slowest = player;
            least = known;
        }
    }
    return slowest >= 0 && std::cmp_less(slowest, names_.size())
               ? std::optional{names_[static_cast<std::size_t>(slowest)]}
               : std::nullopt;
}

std::optional<int> NetGame::network_ms(int player) const {
    if (player <= 0 || player >= net::kMostPlayers) {
        return std::nullopt;  // (the host: nothing to measure)
    }
    if (hosting()) {
        const int peer = peer_of(player);
        return peer >= 0 ? transport_->round_trip_ms(peer) : std::nullopt;
    }
    const std::uint16_t reported = host_network_.at(static_cast<std::size_t>(player));
    return reported != net::kUnknown ? std::optional<int>{reported} : std::nullopt;
}

std::optional<int> NetGame::engine_ms(int player) const {
    if (player <= 0 || player >= net::kMostPlayers) {
        return std::nullopt;
    }
    if (hosting()) {
        return std::cmp_less(player, engine_.size()) ? engine_.at(static_cast<std::size_t>(player)) : std::nullopt;
    }
    const std::uint16_t reported = host_engine_.at(static_cast<std::size_t>(player));
    return reported != net::kUnknown ? std::optional<int>{reported} : std::nullopt;
}

}  // namespace lvr
