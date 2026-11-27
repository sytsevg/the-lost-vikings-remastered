#include "net/match.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace lvr::net {

namespace {

constexpr int kHashEvery = 8;  // the steps whose states are compared: 7, 15, 23, ...
// One joining under way arrives this far on (about 0.8 s): far enough for him to load the level meanwhile, so the
// others don't wait for him.
constexpr std::uint32_t kJoinLead = 16;
// The steps whose comings and goings go with the host's word on who is there (far more than ever late).
constexpr std::uint32_t kRecent = 256;
// One let go is told so, and dropped this long after (or as soon as he says he goes).
constexpr std::uint32_t kPartingMs = 2000;

// The first step from `step` on whose state is hashed (7, 15, 23, ...).
std::uint32_t first_hashed(std::uint32_t step) {
    return step + ((kHashEvery - 1 + kHashEvery - (step % kHashEvery)) % kHashEvery);
}

std::uint16_t as_sent(std::optional<int> ms) {
    return ms ? static_cast<std::uint16_t>(std::clamp(*ms, 0, int{kUnknown} - 1)) : kUnknown;
}

// The changes of who is there from a step on, as the protocol sends them.
std::vector<Presence> presence_from(const Rollback& rollback, std::uint32_t from) {
    const std::vector<PresenceChange> scheduled = rollback.presence_from(from);
    std::vector<Presence> changes;
    changes.reserve(scheduled.size());
    for (const PresenceChange& change : scheduled) {
        changes.push_back({.player = static_cast<std::uint8_t>(change.player), .at = change.at, .there = change.there});
    }
    return changes;
}

Settings settings_for(const Connection& connection, const Started& started) {
    // A place for every player there can be (the game has them too): those not there away, until someone comes.
    return Settings{.players = kMostPlayers,
                    .local = started.player,
                    .delay = started.delay,
                    .window = Match::kWindow,
                    .hash_every = kHashEvery,
                    .first_step = started.first_step,
                    .decides_presence = connection.hosting()};  // (the host's word on who is there, for a guest)
}

}  // namespace

Match::Match(Connection& connection, Game& game, const Started& started)
    : connection_{&connection}, started_{started}, rollback_{game, settings_for(connection, started)} {
    Connection& c = connection;
    const std::uint32_t first = started.first_step;
    for (auto& acked : acked_) {
        acked.fill(first);
    }
    for (int player = 0; player < kMostPlayers; ++player) {
        c.forget_round_trip(player);
    }
    std::vector<std::string> names = c.names();
    names.resize(kMostPlayers);
    c.set_names(std::move(names));
    for (int player = c.seated(); c.hosting() && player < kMostPlayers; ++player) {
        c.unseat(player);  // (a place for each, free)
    }
    hash_sent_ = first_hashed(first);
    heard_host_ = c.now_ms();
    if (started.state.empty()) {  // from the lobby: the places nobody took, away from the start
        for (int player = started.players; player < kMostPlayers; ++player) {
            rollback_.leave(player, 0);
            gone_.at(static_cast<std::size_t>(player)) = true;
        }
    } else {  // under way: who is away in the state given, and this one until he arrives
        for (int player = 0; player < kMostPlayers; ++player) {
            if (!game.here(player)) {
                rollback_.leave(player, first);
                gone_.at(static_cast<std::size_t>(player)) = player != started.player;
            }
        }
        for (const Presence& change : started.presence) {  // the others' comings and goings to come
            if (change.player < kMostPlayers && std::cmp_not_equal(change.player, started.player)) {
                (change.there ? rollback_.arrive(change.player, change.at) : rollback_.leave(change.player, change.at));
                gone_.at(change.player) = !change.there;
            }
        }
        rollback_.arrive(started.player, started.arrives);
        catch_up_to_ = started.arrives;
    }
}

std::expected<Match::Polled, std::string> Match::poll() {
    Connection& c = *connection_;
    c.receive();
    while (const std::optional<Incoming> incoming = c.next()) {
        if (incoming->kind == Incoming::Kind::kLeft) {
            if (!c.hosting()) {
                return std::unexpected{"the host left the game"};
            }
            leaves(incoming->player);
            continue;
        }
        if (incoming->kind != Incoming::Kind::kMessage) {
            continue;
        }
        if (!c.hosting()) {
            heard_host_ = c.now_ms();
            const Message& message = incoming->message;
            if (std::holds_alternative<Back>(message)) {
                return Polled::kBack;  // (the game is over: what comes after is the lobby's)
            }
            if (const auto* leave = std::get_if<Leave>(&message); leave && std::cmp_equal(leave->player, c.player())) {
                return std::unexpected{"dropped by the host"};  // (it held the game up too long)
            }
            if (const auto* resync = std::get_if<Resync>(&message)) {
                if (auto taken = take_resync(*resync); !taken) {
                    return std::unexpected{taken.error()};
                }
                continue;
            }
        }
        take(*incoming);
    }
    put_right();
    if (std::optional<std::string> why = held_up()) {
        return std::unexpected{std::move(*why)};
    }
    for (auto parting = parting_.begin(); parting != parting_.end();) {
        if (c.now_ms() >= parting->second) {
            c.drop_peer(parting->first);
            parting = parting_.erase(parting);
        } else {
            ++parting;
        }
    }
    // What is due goes at most once a refresh from a pass (the window may pass far more often); right after a step
    // it goes at once (flush).
    constexpr auto kRefreshMs = static_cast<std::uint32_t>(kStepMs / 3);
    if (sent_at_ && c.now_ms() - *sent_at_ < kRefreshMs) {
        return Polled::kGoingOn;
    }
    if (auto flushed = flush(); !flushed) {
        return std::unexpected{flushed.error()};
    }
    return Polled::kGoingOn;
}

void Match::take(const Incoming& incoming) {
    Connection& c = *connection_;
    const Message& message = incoming.message;
    const int sender = incoming.player;
    if (const auto* inputs = std::get_if<Inputs>(&message)) {
        take_inputs(sender, *inputs);
    } else if (const auto* status = std::get_if<Status>(&message)) {
        take_status(sender, incoming.peer, *status);
    } else if (const auto* hash = std::get_if<Hash>(&message)) {
        take_hash(sender, *hash);
    } else if (c.hosting()) {
        if (const auto* hello = std::get_if<Hello>(&message); hello != nullptr && sender < 0) {
            welcome_under_way(incoming.peer, *hello);
        } else if (std::holds_alternative<Quit>(message)) {
            leaves(sender);
            c.drop_peer(incoming.peer);
        }
    } else if (const auto* arrive = std::get_if<Arrive>(&message)) {
        if (arrive->player < kMostPlayers) {
            rollback_.arrive(arrive->player, arrive->at);
            c.set_name(arrive->player, arrive->name);
            gone_.at(arrive->player) = false;
        }
    } else if (const auto* decided = std::get_if<Decided>(&message)) {
        if (decided->game == c.game()) {
            std::vector<PresenceChange> changes;
            changes.reserve(decided->presence.size());
            for (const Presence& change : decided->presence) {
                changes.push_back({.player = change.player, .at = change.at, .there = change.there});
            }
            rollback_.know_presence(changes);  // (before the bound: the changes it covers known with it)
            rollback_.decided_until(decided->until);
        }
    } else if (const auto* leave = std::get_if<Leave>(&message)) {
        if (std::cmp_not_equal(leave->player, c.player()) && leave->player < kMostPlayers) {
            rollback_.leave(leave->player, leave->from);
            gone_.at(leave->player) = true;
        }
    }
}

void Match::take_inputs(int sender, const Inputs& inputs) {
    // On the host from his own machine only (a welcomed one); on a guest from the host, who passes everyone's on.
    // And of this game, not one before.
    const Connection& c = *connection_;
    const bool from_him = c.hosting() ? sender > 0 && std::cmp_equal(inputs.player, sender) : true;
    if (from_him && std::cmp_not_equal(inputs.player, c.player()) && inputs.game == c.game()) {
        for (std::size_t i = 0; i < inputs.inputs.size(); ++i) {
            rollback_.add_remote(inputs.player, inputs.first + static_cast<std::uint32_t>(i), inputs.inputs[i]);
        }
    }
}

void Match::take_status(int sender, int peer, const Status& status) {
    Connection& c = *connection_;
    if (sender < 0 || sender >= kMostPlayers || status.game != c.game()) {
        return;
    }
    auto& acked = acked_.at(static_cast<std::size_t>(sender));
    for (std::size_t player = 0; player < acked.size(); ++player) {
        // (Known for ever: that player away; it says nothing of what he gives once he comes.)
        if (status.knows.at(player) != kAll) {
            acked.at(player) = std::max(acked.at(player), status.knows.at(player));
        }
    }
    // Where he is now: where he said, and half the round trip on.
    const int one_way = c.link_ms(peer).value_or(0) / 2;
    const auto later = static_cast<std::uint32_t>(std::lround(one_way / kStepMs));
    rollback_.report(sender, status.step + later, status.advantage);
    if (!c.hosting()) {  // the host's round trips, to the other guests too
        c.reported(status.network, status.engine);
    }
}

void Match::take_hash(int sender, const Hash& hash) {
    // The host: a guest's of his state since the last the host sent him (one of the state before it is let go).
    const Connection& c = *connection_;
    const bool his = c.hosting() ? sender > 0 && std::cmp_equal(hash.player, sender) &&
                                       std::cmp_equal(hash.resyncs, resyncs_.at(static_cast<std::size_t>(sender)))
                                 : hash.player == 0;
    if (hash.game == c.game() && his) {
        rollback_.add_remote_hash(hash.player, hash.step, hash.hash);
    }
}

void Match::put_right() {
    Connection& c = *connection_;
    if (const std::optional<std::uint32_t> out = rollback_.out_of_step(); out && !out_of_step_) {
        out_of_step_ = out;  // (the first this game: the log is written for it)
    }
    if (!c.hosting()) {
        return;  // (the host's word puts it right)
    }
    for (int player = 1; player < kMostPlayers; ++player) {
        if (!rollback_.out_of_step(player) || gone_.at(static_cast<std::size_t>(player)) || c.peer_of(player) < 0) {
            continue;
        }
        // The state before the last step every input is known for: every machine has those inputs (as joining).
        rollback_.catch_up();
        const std::uint32_t from = std::min(rollback_.confirmed(), rollback_.step());
        const int sent = ++resyncs_.at(static_cast<std::size_t>(player));
        c.send(player,
               Resync{.game = c.game(),
                      .player = static_cast<std::uint8_t>(player),
                      .step = from,
                      .resyncs = static_cast<std::uint16_t>(sent),
                      .state = rollback_.state_before(from)},
               Channel::kReliable);
        rollback_.forget_hashes(player);  // (what he says next is of the state sent)
        if (const auto held_back = resyncs_said_.pass(c.now_ms())) {
            log::warn("{}: {} out of step: the host's state sent, of step {} (resync #{}){}", game_and_step(),
                      who(player), from, sent, *held_back == 0 ? "" : std::format(" ({} more held back)", *held_back));
        }
    }
}

std::expected<void, std::string> Match::take_resync(const Resync& resync) {
    Connection& c = *connection_;
    if (resync.game != c.game() || std::cmp_not_equal(resync.player, c.player())) {
        return {};
    }
    if (auto taken = rollback_.take_state(resync.step, resync.state); !taken) {
        return std::unexpected{std::format("the host's state can't be taken: {}", taken.error())};
    }
    resyncs_.at(static_cast<std::size_t>(c.player())) = resync.resyncs;
    taken_at_.push_back(c.now_ms());
    if (taken_at_.size() > static_cast<std::size_t>(kUnsteadyResyncs)) {
        taken_at_.erase(taken_at_.begin());
    }
    // The hashes of steps from there on go again, of the state taken (those sent were of the one before).
    std::erase_if(hashes_, [&resync](const auto& sent) { return sent.first >= resync.step; });
    hash_sent_ = first_hashed(resync.step);
    if (const auto held_back = resyncs_said_.pass(c.now_ms())) {
        log::warn("{}: out of step: the host's state taken, of step {} (resync #{}){}{}", game_and_step(), resync.step,
                  resync.resyncs, unsteady() ? ", the network unsteady" : "",
                  *held_back == 0 ? "" : std::format(" ({} more held back)", *held_back));
    }
    return {};
}

bool Match::unsteady() const {
    return std::cmp_greater_equal(taken_at_.size(), kUnsteadyResyncs) &&
           connection_->now_ms() - taken_at_.front() < kUnsteadyMs;
}

std::optional<std::string> Match::held_up() {
    Connection& c = *connection_;
    const std::optional<std::uint32_t> held = held_ms();
    if (!held) {
        return std::nullopt;
    }
    if (c.hosting()) {
        if (*held >= kHeldMs) {
            if (const std::optional<int> whom = waiting_for(); whom && *whom > 0) {
                const int peer = c.peer_of(*whom);
                const std::uint32_t from = rollback_.known(*whom);
                log::warn("{}: {} let go: the game held up {} s for him", game_and_step(), who(*whom), kHeldMs / 1000);
                ++holds_;  // (a hold as any other: the next is counted from now)
                longest_hold_ = std::max(longest_hold_, *held);
                leaves(*whom);  // (the others told, his Viking passed on)
                if (peer >= 0) {
                    c.send_to_peer(peer, Leave{.player = static_cast<std::uint8_t>(*whom), .from = from},
                                   Channel::kReliable);
                    parting_.emplace_back(peer, c.now_ms() + kPartingMs);
                }
            }
            held_since_ = c.now_ms();  // (another who holds it up has his ten seconds from now)
        }
        return std::nullopt;
    }
    if (*held >= kHeldMs && c.now_ms() - heard_host_ >= kHeldMs) {
        return "no answer from the host";
    }
    if (*held >= kGuestHeldMs) {
        return "the game was held up too long";
    }
    return std::nullopt;
}

std::expected<void, std::string> Match::flush() {
    Connection& c = *connection_;
    const int me = c.player();
    sent_at_ = c.now_ms();
    // To each: every player's inputs he hasn't acknowledged (a guest gives the host his own; the host passes on
    // everyone's but the guest's own), and where this machine is.
    for (int to = 0; to < kMostPlayers; ++to) {
        if (to == me || c.peer_of(to) < 0 || gone_.at(static_cast<std::size_t>(to))) {
            continue;
        }
        for (int whose = 0; whose < kMostPlayers; ++whose) {
            if (whose == to || (!c.hosting() && whose != me)) {
                continue;
            }
            const std::uint32_t from = acked_.at(static_cast<std::size_t>(to)).at(static_cast<std::size_t>(whose));
            const auto unsent = rollback_.known_from(whose, from);
            if (unsent.empty()) {
                continue;
            }
            Inputs message{.game = c.game(),
                           .player = static_cast<std::uint8_t>(whose),
                           .first = unsent.front().first,
                           .inputs = {}};
            for (const auto& [step, input] : unsent) {
                message.inputs.push_back(input);
            }
            c.send(to, message, Channel::kFast);
        }
        Status status{.game = c.game(),
                      .player = static_cast<std::uint8_t>(me),
                      .step = rollback_.step(),
                      .advantage = static_cast<std::int16_t>(rollback_.advantage(to)),
                      .knows = {}};
        for (int player = 0; player < kMostPlayers; ++player) {
            status.knows.at(static_cast<std::size_t>(player)) = rollback_.known(player);
            if (c.hosting()) {
                status.network.at(static_cast<std::size_t>(player)) = as_sent(c.network_ms(player));
                status.engine.at(static_cast<std::size_t>(player)) = as_sent(c.engine_ms(player));
            }
        }
        c.send(to, status, Channel::kFast);
    }
    if (c.hosting()) {
        decide();
    }
    c.echo();
    while (const std::optional<std::uint64_t> hash = rollback_.hash_at(hash_sent_)) {
        c.broadcast(Hash{.game = c.game(),
                         .player = static_cast<std::uint8_t>(me),
                         .step = hash_sent_,
                         .hash = *hash,
                         .resyncs = static_cast<std::uint16_t>(resyncs_.at(static_cast<std::size_t>(me)))},
                    Channel::kReliable);
        hashes_.emplace_back(hash_sent_, *hash);
        hash_sent_ += kHashEvery;
    }
    c.flush();
    put_right();
    return {};
}

void Match::decide() {
    // Who is there is decided before the host's own next step (an arrival is always after it), with the comings and
    // goings it covers: whichever copy comes first is enough, nothing waits for an Arrive. Said every pass, as the
    // fast way wants (only the newest is taken): one said only when it changed, its bundle lost and overtaken by the
    // next, would never come, and a guest counting a player away only as far as it was said would wait for good.
    const std::uint32_t until = rollback_.local_known();
    connection_->broadcast(Decided{.game = connection_->game(),
                                   .until = until,
                                   .presence = presence_from(rollback_, until - std::min(until, kRecent))},
                           Channel::kFast);
}

void Match::welcome_under_way(int peer, const Hello& hello) {
    Connection& c = *connection_;
    std::optional<std::string> refused = c.refusal(hello);
    int place = -1;
    for (int player = 1; player < kMostPlayers && place < 0 && !refused; ++player) {
        if (gone_.at(static_cast<std::size_t>(player)) && c.peer_of(player) < 0) {
            place = player;
        }
    }
    if (!refused && place < 0) {
        refused = "the game is full";
    }
    if (refused) {
        log::info("{}: a hello turned away: {}", game_and_step(), *refused);
        c.send_to_peer(peer, Refused{.why = *refused}, Channel::kReliable);
        c.drop_peer(peer);
        return;
    }
    rollback_.catch_up();  // (a correction still to run: the states kept are right again)
    // The state before the last step every input is known for (all machines have it so), and his first step: past
    // any the host's input is known for, so no machine has confirmed it.
    const std::uint32_t from = std::min(rollback_.confirmed(), rollback_.step());
    const std::uint32_t arrives = std::max(rollback_.step(), rollback_.local_known()) + kJoinLead;
    const auto at = static_cast<std::size_t>(place);
    c.seat(place, peer);
    c.set_name(place, shown_name(hello.name, place));
    gone_.at(at) = false;
    resyncs_.at(at) = 0;
    rollback_.forget_hashes(place);  // (the one before in his place said them)
    acked_.at(at).fill(from);
    c.forget_round_trip(place);
    c.send(place,
           Joining{.game = c.game(),
                   .player = static_cast<std::uint8_t>(place),
                   .step = from,
                   .arrives = arrives,
                   .delay = static_cast<std::uint8_t>(started_.delay),
                   .names = c.names(),
                   .state = rollback_.state_before(from),
                   .presence = presence_from(rollback_, from)},
           Channel::kReliable);
    rollback_.arrive(place, arrives);
    log::info("{}: {} joins the game under way, from the state of step {}, arriving at step {}", game_and_step(),
              who(place), from, arrives);
    c.broadcast(Arrive{.player = static_cast<std::uint8_t>(place), .at = arrives, .name = c.names().at(at)},
                Channel::kReliable);
}

void Match::leaves(int player) {
    if (player <= 0 || player >= kMostPlayers || gone_.at(static_cast<std::size_t>(player))) {
        return;
    }
    const std::uint32_t from = rollback_.known(player);  // what he gave counts; after that, nothing
    log::info("{}: {} left (his inputs up to step {} count)", game_and_step(), who(player), from);
    rollback_.leave(player, from);
    gone_.at(static_cast<std::size_t>(player)) = true;
    connection_->unseat(player);  // (the place is free to come into again)
    connection_->broadcast(Leave{.player = static_cast<std::uint8_t>(player), .from = from}, Channel::kReliable);
}

bool Match::wants_input() const {
    return rollback_.local_known() <= rollback_.step() + static_cast<std::uint32_t>(started_.delay);
}

void Match::give(const Input& input) {
    (void)rollback_.add_local(input);
}

bool Match::run_to_arrival() {
    while (rollback_.step() < catch_up_to_ && rollback_.advance(false)) {
    }
    return rollback_.step() >= catch_up_to_;
}

bool Match::step() {
    // Held up while no step runs but for this machine's own input (that one is the front end's to give).
    const auto hold = [this] {
        if (!held_since_) {
            held_since_ = connection_->now_ms();
        }
        held_for_ = waiting_for();
    };
    if (rollback_.ahead()) {
        rollback_.catch_up();  // a refresh to let the others catch up
        hold();
        return false;
    }
    if (!rollback_.advance()) {
        ++waited_;
        if (rollback_.local_known() > rollback_.step()) {
            hold();
        }
        return false;
    }
    waited_ = 0;
    if (const std::optional<std::uint32_t> held = held_ms(); held && *held >= kNoticedMs) {  // (it was seen)
        ++holds_;
        longest_hold_ = std::max(longest_hold_, *held);
        if (const auto held_back = holds_said_.pass(connection_->now_ms())) {
            log::info("{}: held up {:.1f} s{}{}", game_and_step(), *held / 1000.0,
                      held_for_ ? std::format(" for {}", who(*held_for_)) : std::string{},
                      *held_back == 0 ? "" : std::format(" ({} more held back)", *held_back));
        }
    }
    held_since_.reset();
    held_for_.reset();
    return true;
}

std::optional<std::uint32_t> Match::held_ms() const {
    return held_since_ ? std::optional{connection_->now_ms() - *held_since_} : std::nullopt;
}

std::optional<int> Match::waiting_for() const {
    if (waited_ == 0 && !held_since_) {
        return std::nullopt;
    }
    std::optional<int> slowest;
    std::uint32_t least = 0;
    for (int player = 0; player < kMostPlayers; ++player) {
        if (player == connection_->player() || gone_.at(static_cast<std::size_t>(player))) {
            continue;
        }
        if (const std::uint32_t known = rollback_.known(player); !slowest || known < least) {
            slowest = player;
            least = known;
        }
    }
    return slowest;
}

std::string Match::who(int player) const {
    const std::vector<std::string>& names = connection_->names();
    if (player >= 0 && std::cmp_less(player, names.size()) && !names.at(static_cast<std::size_t>(player)).empty()) {
        return std::format("player {} ({})", player + 1, names.at(static_cast<std::size_t>(player)));
    }
    return std::format("player {}", player + 1);
}

std::string Match::game_and_step() const {
    return std::format("game {} step {}", connection_->game(), rollback_.step());
}

std::string Match::summary() const {
    const Connection& c = *connection_;
    int resyncs = 0;
    for (int player = 0; player < kMostPlayers; ++player) {
        if (c.hosting() ? player > 0 : player == c.player()) {
            resyncs += resyncs_.at(static_cast<std::size_t>(player));
        }
    }
    return std::format(
        "ended at step {}: {} resync(s) {}; held up {} time(s){}; {} rollback(s), the deepest {} step(s), {} "
        "step(s) run again",
        rollback_.step(), resyncs, c.hosting() ? "sent" : "taken", holds_,
        holds_ == 0 ? std::string{} : std::format(", the longest {:.1f} s", longest_hold_ / 1000.0),
        rollback_.rollbacks(), rollback_.deepest_rollback(), rollback_.steps_run_again());
}

void Match::back_to_lobby() {
    if (connection_->hosting()) {
        connection_->broadcast(Back{}, Channel::kReliable);
    }
}

void Match::quit() {
    connection_->quit();
}

std::string Match::log() const {
    std::string text = std::format("coop 1\nbuild {}\nslot {}\nplayers {}\ncheat {}\nplayer {}\nfirst {}\n",
                                   connection_->identity().build, started_.slot, started_.players,
                                   started_.cheat ? 1 : 0, started_.player, started_.first_step);
    const std::uint32_t confirmed = std::min(rollback_.confirmed(), rollback_.step());
    for (std::uint32_t step = started_.first_step; step < confirmed; ++step) {
        const std::optional<std::vector<Input>> inputs = rollback_.inputs_at(step);
        if (!inputs) {
            break;
        }
        text += std::format("step {}", step);
        for (const Input& input : *inputs) {
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

}  // namespace lvr::net
