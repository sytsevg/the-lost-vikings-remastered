#include "lvr/online.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "lvr/archive.hpp"

namespace lvr {

namespace {

constexpr int kWindow = 8;  // the steps rollback can run ahead (ADR-0012)
constexpr double kStepMilliseconds = 1000.0 * 3.0 / kDisplayHz;

// The input delay for these round trips: none under 50 ms, a step under 150, else two.
int delay_for(int round_trip_ms) {
    constexpr int kNone = 50;
    constexpr int kOne = 150;
    return round_trip_ms < kNone ? 0 : (round_trip_ms < kOne ? 1 : 2);
}

}  // namespace

NetGame::NetGame(std::unique_ptr<net::Transport> transport, Identity identity)
    : transport_{std::move(transport)}, identity_{std::move(identity)} {}

std::expected<std::unique_ptr<NetGame>, std::string> NetGame::host(std::uint16_t port, int players, int slot,
                                                                   bool cheat, std::optional<int> delay,
                                                                   Identity identity) {
    auto transport = net::Transport::host(port);
    if (!transport) {
        return std::unexpected{transport.error()};
    }
    std::unique_ptr<NetGame> game{new NetGame{std::move(*transport), std::move(identity)}};
    game->wanted_ = std::clamp(players, 1, net::kMostPlayers);
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
    for (const net::Event& event : transport_->poll()) {
        if (event.kind == net::Event::Kind::kLeft) {
            if (!hosting()) {
                return std::unexpected{"the host is gone"};
            }
            if (const int left = player_of(event.peer); left > 0) {  // the ones after him move up
                peers_.erase(peers_.begin() + left);
                names_.erase(names_.begin() + left);
                for (std::size_t player = 1; player < peers_.size(); ++player) {
                    send(peers_[player], net::Welcome{.player = static_cast<std::uint8_t>(player)},
                         net::Channel::kReliable);
                }
                broadcast(net::Lobby{.names = names_}, net::Channel::kReliable);
            }
            continue;
        }
        if (event.kind != net::Event::Kind::kMessage) {
            continue;
        }
        const std::optional<net::Message> message = net::decode(event.bytes);
        if (!message) {
            continue;
        }
        if (hosting()) {
            const auto* hello = std::get_if<net::Hello>(&*message);
            if (hello == nullptr || player_of(event.peer) >= 0) {
                continue;
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
                continue;
            }
            const auto player = static_cast<std::uint8_t>(names_.size());
            peers_.push_back(event.peer);
            names_.push_back(net::shown_name(hello->name, player));
            send(event.peer, net::Welcome{.player = player}, net::Channel::kReliable);
            broadcast(net::Lobby{.names = names_}, net::Channel::kReliable);
            continue;
        }
        if (const auto* welcome = std::get_if<net::Welcome>(&*message)) {
            player_ = welcome->player;
        } else if (const auto* lobby = std::get_if<net::Lobby>(&*message)) {
            names_ = lobby->names;
        } else if (const auto* refused = std::get_if<net::Refused>(&*message)) {
            return std::unexpected{std::format("refused: {}", refused->why)};
        } else if (const auto* start = std::get_if<net::Start>(&*message)) {
            names_ = start->names;
            joined_ = Joined{.player = player_,
                             .players = start->players,
                             .delay = start->delay,
                             .slot = start->slot,
                             .cheat = start->cheat,
                             .names = start->names};
            return joined_;
        }
    }
    if (!hosting() || std::cmp_less(names_.size(), wanted_)) {
        return std::nullopt;
    }
    // Everyone is there: the delay from the longest round trip, once each has one.
    int longest = 0;
    for (std::size_t player = 1; player < peers_.size(); ++player) {
        const std::optional<int> round_trip = transport_->round_trip_ms(peers_[player]);
        if (!round_trip && !delay_) {
            return std::nullopt;
        }
        longest = std::max(longest, round_trip.value_or(0));
    }
    const int delay = delay_.value_or(delay_for(longest));
    broadcast(net::Start{.players = static_cast<std::uint8_t>(names_.size()),
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
                     .names = names_};
    return joined_;
}

void NetGame::begin(Session& session) {
    const Joined joined = joined_.value_or(Joined{});  // (the lobby's, once it started)
    delay_steps_ = joined.delay;
    const net::Settings settings{
        .players = joined.players, .local = joined.player, .delay = joined.delay, .window = kWindow};
    game_ = std::make_unique<CoopGame>(session, static_cast<std::size_t>(kWindow) + 2);
    rollback_ = std::make_unique<net::Rollback>(*game_, settings);
    acked_.assign(static_cast<std::size_t>(joined.players), {});
    gone_.assign(static_cast<std::size_t>(joined.players), false);
}

void NetGame::leaves(int player) {
    if (player <= 0 || gone_.at(static_cast<std::size_t>(player))) {
        return;
    }
    const std::uint32_t from = rollback_->known(player);  // what he gave counts; after that, nothing
    rollback_->leave(player, from);
    gone_.at(static_cast<std::size_t>(player)) = true;
    broadcast(net::Leave{.player = static_cast<std::uint8_t>(player), .from = from}, net::Channel::kReliable);
}

std::expected<void, std::string> NetGame::take(int peer, const net::Message& message) {
    const int sender = player_of(peer);
    if (const auto* inputs = std::get_if<net::Inputs>(&message)) {
        if (std::cmp_not_equal(inputs->player, player_)) {
            for (std::size_t i = 0; i < inputs->inputs.size(); ++i) {
                rollback_->add_remote(inputs->player, inputs->first + static_cast<std::uint32_t>(i), inputs->inputs[i]);
            }
        }
    } else if (const auto* status = std::get_if<net::Status>(&message)) {
        if (sender >= 0 && std::cmp_less(sender, acked_.size())) {
            auto& acked = acked_.at(static_cast<std::size_t>(sender));
            for (std::size_t player = 0; player < acked.size(); ++player) {
                acked.at(player) = std::max(acked.at(player), status->knows.at(player));
            }
            // Where he is now: where he said, and half the round trip on.
            const int one_way = transport_->round_trip_ms(peer).value_or(0) / 2;
            const auto later = static_cast<std::uint32_t>(std::lround(one_way / kStepMilliseconds));
            rollback_->report(sender, status->step + later, status->advantage);
        }
    } else if (const auto* hash = std::get_if<net::Hash>(&message)) {
        rollback_->add_remote_hash(hash->player, hash->step, hash->hash);
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
    for (const net::Event& event : transport_->poll()) {
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
            net::Inputs message{
                .player = static_cast<std::uint8_t>(whose), .first = unsent.front().first, .inputs = {}};
            for (const auto& [step, input] : unsent) {
                message.inputs.push_back(input);
            }
            send(peer, message, net::Channel::kFast);
        }
        net::Status status{.player = static_cast<std::uint8_t>(player_),
                           .step = rollback_->step(),
                           .advantage = static_cast<std::int16_t>(rollback_->advantage(to)),
                           .knows = {}};
        for (int player = 0; player < players; ++player) {
            status.knows.at(static_cast<std::size_t>(player)) = rollback_->known(player);
        }
        send(peer, status, net::Channel::kFast);
    }
    while (const std::optional<std::uint64_t> hash = rollback_->hash_at(hash_sent_)) {
        broadcast(net::Hash{.player = static_cast<std::uint8_t>(player_), .step = hash_sent_, .hash = *hash},
                  net::Channel::kReliable);
        hash_sent_ += 8;
    }
    if (const std::optional<std::uint32_t> out = rollback_->out_of_step()) {
        return std::unexpected{std::format("out of step with another machine at step {}", *out)};
    }
    return {};
}

bool NetGame::wants_input() const {
    return rollback_->local_known() <= rollback_->step() + static_cast<std::uint32_t>(delay_steps_);
}

void NetGame::give(const StepInput& input) {
    (void)rollback_->add_local(pack(input, player_));
}

std::optional<std::vector<Screen>> NetGame::step() {
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

std::optional<int> NetGame::latency_ms(int player) const {
    const int peer = peer_of(player);
    return peer >= 0 && player != player_ ? transport_->round_trip_ms(peer) : std::nullopt;
}

}  // namespace lvr
