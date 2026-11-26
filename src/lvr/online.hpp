#pragma once

// Co-op online's front end (ADR-0012), as the game's window loop asks for it a refresh at a time: the connection,
// its lobby (net::Lobby) and its games (net::Match) over the session. (Until the places take its part, step 3.)

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lvr/coop.hpp"
#include "lvr/session.hpp"
#include "net/connection.hpp"
#include "net/lobby.hpp"
#include "net/match.hpp"
#include "net/transport.hpp"

namespace lvr {

class NetGame {
  public:
    // What every machine starts the game with, and which player this one is (joining under way: `slot` the scene
    // of the state given).
    using Joined = net::Started;
    // Who this build and these game files are, as a guest says in its hello.
    struct Identity {
        std::string build;
        std::uint64_t game = 0;
        std::string name;
    };

    // Hosts through `transport` (a host's), the game to start at `slot` when the host says so.
    static std::unique_ptr<NetGame> host(std::unique_ptr<net::Transport> transport, int slot, const Identity& identity);
    // Joins the game hosted at the other end of `transport` (a guest's): says hello.
    static std::unique_ptr<NetGame> join(std::unique_ptr<net::Transport> transport, const Identity& identity);

    // The lobby, a poll a call: once the game starts, what it is. An error: refused, or the host gone.
    std::expected<std::optional<Joined>, std::string> lobby();
    const std::vector<std::string>& names() const noexcept { return connection_.names(); }
    bool welcomed() const noexcept { return lobby_ == nullptr || lobby_->welcomed(); }
    int slot() const noexcept { return lobby_ != nullptr ? lobby_->slot() : slot_; }
    void set_slot(int slot);
    void start();
    void cancel_start();
    int countdown() const { return lobby_ != nullptr ? lobby_->countdown() : 0; }
    // Host, in the game: everyone back to the lobby, this machine at once (to_lobby). A guest is told in a poll
    // (back_asked).
    void back_to_lobby();
    bool back_asked() const noexcept { return back_; }
    // The game over for this machine, the connection kept: the lobby again.
    void to_lobby();

    // The game: rollback over `session` (made for the Joined game).
    void begin(Session& session);
    std::expected<void, std::string> poll();
    std::expected<void, std::string> flush();
    bool wants_input() const { return match_->wants_input(); }
    void give(const StepInput& input);
    // The next step if it can run now: its pictures. None: this refresh waits.
    std::optional<std::vector<Screen>> step();
    std::uint32_t at_step() const noexcept { return match_ != nullptr ? match_->at_step() : 0; }
    // Whom it is waiting for (his name), and for how long (refreshes); none: it isn't.
    std::optional<std::string> waiting_for() const;
    int waited() const noexcept { return match_ != nullptr ? match_->waited() : 0; }
    std::optional<int> network_ms(int player) const { return connection_.network_ms(player); }
    std::optional<int> engine_ms(int player) const { return connection_.engine_ms(player); }
    int player() const noexcept { return connection_.player(); }
    // This machine leaves the game: a guest tells the host (and waits a moment for that to go).
    void quit() { connection_.quit(); }
    // Where the game's log is written if the machines go out of step (a folder; none: not written).
    void set_log_folder(std::filesystem::path folder) { log_folder_ = std::move(folder); }
    std::string log() const { return match_ != nullptr ? match_->log() : std::string{}; }
    bool hosting() const noexcept { return connection_.hosting(); }

  private:
    NetGame(std::unique_ptr<net::Transport> transport, const Identity& identity);
    // What a pass of the game came to: out of step, the log written (once) and said where.
    std::expected<void, std::string> logged(std::expected<void, std::string> result);
    void names_to_session();

    net::Connection connection_;
    std::unique_ptr<net::Lobby> lobby_;
    std::unique_ptr<CoopGame> game_;
    std::unique_ptr<net::Match> match_;
    Session* session_ = nullptr;
    std::optional<Joined> joined_;
    std::vector<std::string> names_given_;  // the names the session was last given
    std::filesystem::path log_folder_;
    int slot_ = 0;
    bool back_ = false;  // a guest: told to go back to the lobby
    bool logged_ = false;
};

}  // namespace lvr
