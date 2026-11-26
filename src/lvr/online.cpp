#include "lvr/online.hpp"

#include <format>
#include <fstream>
#include <utility>

#include "lvr/archive.hpp"

namespace lvr {

NetGame::NetGame(std::unique_ptr<net::Transport> transport, const Identity& identity)
    : connection_{
          std::move(transport),
          net::Identity{
              .build = identity.build, .game = identity.game, .state_version = kStateVersion, .name = identity.name}} {}

std::unique_ptr<NetGame> NetGame::host(std::unique_ptr<net::Transport> transport, int slot, const Identity& identity) {
    std::unique_ptr<NetGame> game{new NetGame{std::move(transport), identity}};
    game->slot_ = slot;
    game->lobby_ = net::host_lobby(game->connection_, slot, false);
    return game;
}

std::unique_ptr<NetGame> NetGame::join(std::unique_ptr<net::Transport> transport, const Identity& identity) {
    std::unique_ptr<NetGame> game{new NetGame{std::move(transport), identity}};
    game->lobby_ = net::guest_lobby(game->connection_, false);
    return game;
}

std::expected<std::optional<NetGame::Joined>, std::string> NetGame::lobby() {
    if (lobby_ == nullptr) {
        return std::nullopt;
    }
    auto started = lobby_->poll();
    if (!started || !*started) {
        return started;
    }
    Joined joined = **started;
    if (!joined.state.empty()) {  // under way: the scene the state is of
        joined.slot = Session::scene_of(joined.state).value_or(0);
    }
    slot_ = lobby_->slot();
    lobby_.reset();
    joined_ = joined;
    return joined;
}

void NetGame::set_slot(int slot) {
    slot_ = slot;
    if (lobby_ != nullptr) {
        lobby_->set_slot(slot);
    }
}

void NetGame::start() {
    if (lobby_ != nullptr) {
        lobby_->start();
    }
}

void NetGame::cancel_start() {
    if (lobby_ != nullptr) {
        lobby_->cancel_start();
    }
}

void NetGame::back_to_lobby() {
    if (hosting()) {
        if (match_ != nullptr) {
            match_->back_to_lobby();
        }
        to_lobby();
    }
}

void NetGame::to_lobby() {
    match_.reset();
    game_.reset();
    session_ = nullptr;
    joined_.reset();
    names_given_.clear();
    back_ = false;
    logged_ = false;
    lobby_ = hosting() ? net::host_lobby(connection_, slot_, false) : net::guest_lobby(connection_, true, slot_);
}

void NetGame::begin(Session& session) {
    session_ = &session;
    game_ = std::make_unique<CoopGame>(session);
    match_ = std::make_unique<net::Match>(connection_, *game_, joined_.value_or(Joined{}));
    names_to_session();
}

std::expected<void, std::string> NetGame::poll() {
    if (back_ || match_ == nullptr) {
        return {};  // (going back to the lobby: what comes is the lobby's)
    }
    auto polled = match_->poll();
    if (!polled) {
        return logged(std::unexpected{polled.error()});
    }
    back_ = *polled == net::Match::Polled::kBack;
    names_to_session();
    return {};
}

std::expected<void, std::string> NetGame::flush() {
    return match_ != nullptr ? logged(match_->flush()) : std::expected<void, std::string>{};
}

std::expected<void, std::string> NetGame::logged(std::expected<void, std::string> result) {
    if (result || !match_->out_of_step()) {
        return result;
    }
    std::string written;
    if (!logged_ && !log_folder_.empty()) {  // what went before, for lv-replay --coop
        logged_ = true;
        std::error_code ignored;
        std::filesystem::create_directories(log_folder_, ignored);
        const std::filesystem::path file =
            log_folder_ / std::format("coop-{}-player{}.log", connection_.identity().build, player() + 1);
        std::ofstream out_file{file};
        out_file << match_->log();
        written = out_file ? std::format(" (the game's log: {})", file.string()) : std::string{};
    }
    return std::unexpected{result.error() + written};
}

void NetGame::give(const StepInput& input) {
    match_->give(pack(input, player()));
}

std::optional<std::vector<Screen>> NetGame::step() {
    if (match_->joining()) {  // joined under way: up to his arrival at once, unseen, then fading in
        const bool there = match_->run_to_arrival();
        session_->drop_carried_effects();
        if (!there) {
            return std::nullopt;  // (the others' inputs for the rest still on their way)
        }
        session_->fade_in();
    }
    if (!match_->step()) {
        return std::nullopt;
    }
    return game_->take_pictures();
}

std::optional<std::string> NetGame::waiting_for() const {
    const std::optional<int> whom = match_ != nullptr ? match_->waiting_for() : std::nullopt;
    return whom && std::cmp_less(*whom, names().size()) ? std::optional{names().at(static_cast<std::size_t>(*whom))}
                                                        : std::nullopt;
}

void NetGame::names_to_session() {
    if (session_ != nullptr && names() != names_given_) {
        names_given_ = names();
        session_->set_player_names(names_given_);
    }
}

}  // namespace lvr
