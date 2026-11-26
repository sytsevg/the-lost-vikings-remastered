#pragma once

// The game's front end (GLOSSARY.md): where the game as a whole is for the person at the machine, a place at a time,
// and the moves between them (ADR-0012). Offline: the game alone (the title, its levels and scenes; the only place
// under Vanilla rules). Connecting: a guest waiting for the host's answer. Lobby: everyone waiting before a game
// online. Online: a game online, played.
//
// The window's loop (main.cpp) plays the current place a refresh at a time: it reads the keyboard and the gamepads,
// runs the menu, shows the pictures and plays the sound; the place says what is played and shown, and where to go
// next (Go). Places makes each move, in one spot. Nothing here knows windows or devices. (Not lvr/places.*: where a
// world keeps its animation programs.)

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lvr/coop.hpp"
#include "lvr/menu.hpp"
#include "lvr/profile.hpp"
#include "lvr/session.hpp"
#include "lvr/showcase.hpp"
#include "net/connection.hpp"
#include "net/lobby.hpp"
#include "net/match.hpp"
#include "net/transport.hpp"

namespace lvr {

// Where to go from a place.
struct Go {
    enum class To : std::uint8_t { kStay, kOffline, kConnecting, kLobby, kOnline, kDone };
    To to = To::kStay;
    std::string why;      // to Offline: why the game online ended, if it wasn't the player's wish
    bool failed = false;  // the game ended with an error (the program's status)
    bool host = false;    // from Offline to the Lobby: hosting; and the level its password named (-1: a new game)
    int level = 0;
};

// A step's input as the window's loop gathered it: the keyboard word and the gamepads' (with what the menu asked
// of the original's boxes), the menus' keys, the stick's pace; and Escape pressed since the last step.
struct Tick {
    std::uint16_t keys = 0;
    Play::MenuKeys menu_keys;
    Pace pace;
    bool escaped = false;
};

// What the pictures to come are: each with what it asks of the sound; whether this machine's input was taken
// (online it can't always be); and where to go.
struct Stepped {
    std::vector<Screen> frames;
    std::vector<std::vector<SoundCue>> cues;
    bool took_input = true;
    Go go;
};

// The window's menu as a place sees it: open or not (`menu`), opened as the window opens it (with the place's
// choices, as Escape opens it), its picture drawn again. A window's, or one without (headless_ui).
class Ui {
  public:
    Ui(std::optional<Menu>& menu, std::function<Menu&()> open, std::function<void()> redraw)
        : menu_{&menu}, open_{std::move(open)}, redraw_{std::move(redraw)} {}
    Menu* menu() { return *menu_ ? &**menu_ : nullptr; }
    Menu& open_menu() { return open_(); }
    void redraw() {
        if (redraw_) {
            redraw_();
        }
    }

  private:
    std::optional<Menu>* menu_;
    std::function<Menu&()> open_;
    std::function<void()> redraw_;
};

class Place {
  public:
    Place() = default;
    Place(const Place&) = delete;
    Place& operator=(const Place&) = delete;
    Place(Place&&) = delete;
    Place& operator=(Place&&) = delete;
    virtual ~Place() = default;

    // What it plays or shows: its sound is heard (if `heard`), its font draws the menu and the boxes, Remaster's own
    // keys act on it (zoom, minimap, quicksave: where it takes them).
    virtual Session& session() = 0;
    virtual bool heard() const { return true; }
    // Its session is its own, new: the player's settings go to it as it begins (a game online's).
    virtual bool own_session() const { return false; }
    // Whether the game holds while the menu is open (online it goes on under it).
    virtual bool holds() const { return true; }
    // The menu's choices here.
    virtual Menu::Choices choices() = 0;
    // The place begins in the window: the page its menu opens on, if any.
    virtual void begin(Ui& ui) { (void)ui; }
    // Every refresh: what came, and the menu's page as it is now.
    virtual Go refresh(Ui& ui) {
        (void)ui;
        return {};
    }
    // The pictures to come, when the last are used up.
    virtual Stepped step(const Tick& tick, Ui& ui) = 0;
    // What the menu asked. Handled: the menu stays as it is (else, something asked, it closes).
    struct Answer {
        bool handled = false;
        Go go;
    };
    virtual Answer asked(Menu::Asked asked, Menu& menu) {
        (void)asked;
        (void)menu;
        return {};
    }
    // What is laid over the picture while the menu is closed.
    virtual CoopOverlay overlay() const { return {}; }
    // Left, for `go`.
    virtual void leave(const Go& go) { (void)go; }
};

// The menu without a window (a dump, a test): kept in `menu`, opened with the place's choices and the settings
// given, drawn nowhere.
Ui headless_ui(std::optional<Menu>& menu, Place& place, Settings settings = {});

// What the menu has to choose from: the Modern sound if there is one, the screen's looks, the passwords (HOST GAME's
// level); online, whether this machine hosts a game from the title's lobby (RETURN TO LOBBY).
Menu::Choices menu_choices(const Session& session, bool hosting = false, bool lobby = false);
// What a game online starts with, as the lobby says it: a new game (the intro, as the title's NEW GAME), or a level
// and its password.
std::string start_words(const Session& session, int slot);
// What the menu says of something that went wrong: in the font's capitals, as much as fits its box.
std::string said(std::string_view text);
// An address to join a game online at: the host's, and the port after a colon (none, or one that isn't a port: the
// default). An IPv6 address has colons of its own: with a port it is in brackets ([::1]:47230); without, as it is.
std::pair<std::string, std::uint16_t> split_address(std::string_view address);

// The game alone: the title, its levels and scenes. Its session is the program's, kept while the game is online and
// gone back to after. Under Remaster rules in a window (`may_go_online`), the title's HOST GAME and JOIN GAME open
// their pages, and lead online.
class OfflinePlace final : public Place {
  public:
    OfflinePlace(Session& session, Showcase* showcase, bool may_go_online)
        : session_{&session}, showcase_{showcase}, may_go_online_{may_go_online} {}

    Session& session() override { return *session_; }
    Menu::Choices choices() override { return menu_choices(*session_); }
    // Back from a game online that ended for a reason not the player's wish: its page (HOST or JOIN) says why.
    void tell(bool host, std::string why);
    void begin(Ui& ui) override;
    Stepped step(const Tick& tick, Ui& ui) override;
    Answer asked(Menu::Asked asked, Menu& menu) override;

  private:
    void open_page(Ui& ui, bool host, std::string_view said);

    Session* session_;
    Showcase* showcase_;
    std::optional<std::pair<bool, std::string>> tell_;  // (hosting, why)
    bool may_go_online_;
};

// A guest waiting for the host's answer, a box over a still of the title (no sound meanwhile): into the lobby once
// he has a place, or into the game if it is under way. Escape gives up; ten seconds without an answer, the same.
class ConnectingPlace final : public Place {
  public:
    using Join = std::function<std::expected<std::unique_ptr<net::Transport>, std::string>(const std::string& address,
                                                                                           std::uint16_t port)>;
    ConnectingPlace(Session& offline, Join join, net::Identity identity, const std::string& address);

    Session& session() override { return *offline_; }
    bool heard() const override { return false; }
    Menu::Choices choices() override { return menu_choices(*offline_); }
    Go refresh(Ui& ui) override;
    Stepped step(const Tick& tick, Ui& ui) override;
    CoopOverlay overlay() const override { return overlay_; }

    // For the move on: the connection, the guest's lobby, and the game if it is under way.
    std::unique_ptr<net::Connection> take_connection() { return std::move(connection_); }
    std::unique_ptr<net::GuestLobby> take_lobby() { return std::move(lobby_); }
    const std::optional<net::Started>& started() const noexcept { return started_; }

  private:
    Session* offline_;
    Join join_;
    net::Identity identity_;
    std::string address_;
    std::uint16_t port_ = 0;
    CoopOverlay overlay_;
    std::unique_ptr<net::Connection> connection_;
    std::unique_ptr<net::GuestLobby> lobby_;
    std::optional<net::Started> started_;
    std::uint32_t began_ = 0;  // when the connection was made (the connection's clock)
    int refreshes_ = 0;
};

// The lobby, over a still of the title: who is there and their round trips, what the game starts with; the host's
// START (after a countdown) and its cancelling; LEAVE.
class LobbyPlace final : public Place {
  public:
    // The host's lobby, or a guest's.
    LobbyPlace(Session& offline, std::unique_ptr<net::Connection> connection, std::unique_ptr<net::HostLobby> lobby);
    LobbyPlace(Session& offline, std::unique_ptr<net::Connection> connection, std::unique_ptr<net::GuestLobby> lobby);

    Session& session() override { return *offline_; }
    Menu::Choices choices() override { return menu_choices(*offline_); }
    void begin(Ui& ui) override;
    Go refresh(Ui& ui) override;
    Stepped step(const Tick& tick, Ui& ui) override;
    Answer asked(Menu::Asked asked, Menu& menu) override;

    // For the move into the game: the connection and the game it starts.
    std::unique_ptr<net::Connection> take_connection() { return std::move(connection_); }
    const std::optional<net::Started>& started() const noexcept { return started_; }

  private:
    Session* offline_;
    std::unique_ptr<net::Connection> connection_;
    std::unique_ptr<net::HostLobby> host_;  // (one of the two)
    std::unique_ptr<net::GuestLobby> guest_;
    std::optional<net::Started> started_;
};

// A game online, played: its own session under rollback (net::Match) over the connection. The names under the
// Vikings' portraits and whom the game waits for laid over it; the menu over the game going on, darkened, with the
// players and their round trips; the host's RETURN TO LOBBY. A guest's game ends when he leaves, the host is gone, or
// the host ends it (the host's own end is the program's, as offline).
class OnlinePlace final : public Place {
  public:
    struct Needs {
        std::filesystem::path dir;  // the game's files
        SessionSound sound;
        Draw draw = lvr::draw;
        std::filesystem::path logs;  // where its log is written if the machines go out of step (none: not)
    };
    // The game `started` (the lobby's, or one under way at the state given), its session opened.
    static std::expected<std::unique_ptr<OnlinePlace>, std::string> open(std::unique_ptr<net::Connection> connection,
                                                                         net::Started started, const Needs& needs);

    Session& session() override { return *session_; }
    bool own_session() const override { return true; }
    bool holds() const override { return false; }
    Menu::Choices choices() override { return menu_choices(*session_, connection_->hosting(), true); }
    Go refresh(Ui& ui) override;
    Stepped step(const Tick& tick, Ui& ui) override;
    Answer asked(Menu::Asked asked, Menu& menu) override;
    CoopOverlay overlay() const override;
    void leave(const Go& go) override;

    // Back to the lobby: the connection (the host's: everyone told), and the scene the game started in.
    std::unique_ptr<net::Connection> take_connection();
    int slot() const noexcept { return started_.slot; }
    // The next step it runs, and the game's log (what lv-replay --coop plays again).
    std::uint32_t at_step() const noexcept { return match_.at_step(); }
    std::string log() const { return match_.log(); }

  private:
    OnlinePlace(std::unique_ptr<net::Connection> connection, net::Started started, std::unique_ptr<Session> session,
                std::filesystem::path logs);
    // An error of the game's pass: out of step, its log written (once) and said where.
    std::string logged(const std::string& error);
    void names_to_session();
    // The menu's page of the players: each one there, his round trips to the host.
    std::vector<Menu::PlayerLine> player_lines() const;

    std::unique_ptr<net::Connection> connection_;
    net::Started started_;
    std::unique_ptr<Session> session_;
    CoopGame game_;
    net::Match match_;
    std::filesystem::path logs_;
    std::vector<std::string> names_given_;  // the names the session was last given
    Screen last_;                           // the last picture: shown again while the game waits
    bool logged_ = false;
};

// The places, and each move between them: Offline always there, and one of the others while online.
class Places {
  public:
    struct Needs {
        OnlinePlace::Needs online;
        const Settings* settings = nullptr;  // the player's: the port to host on, the address to join, his name
        net::Identity identity;              // this build and these game files (the name the settings')
        // How the network is reached: SDL3_net in the game, a simulated network in tests.
        std::function<std::expected<std::unique_ptr<net::Transport>, std::string>(std::uint16_t port)> host;
        ConnectingPlace::Join join;
    };
    Places(OfflinePlace& offline, Needs needs) : offline_{&offline}, needs_{std::move(needs)}, current_{&offline} {}

    Place& current() noexcept { return *current_; }
    bool done() const noexcept { return done_; }
    bool failed() const noexcept { return failed_; }
    // The current place has said where to go: there, made as the move needs.
    void go(const Go& go);

  private:
    net::Identity identity() const;  // (the name the settings' now)
    void offline(bool host, const std::string& why);
    void host(int level);
    void online(std::unique_ptr<net::Connection> connection, const net::Started& started);
    // Into a place online, out of the one it was (what it hands on taken already).
    void enter(std::unique_ptr<ConnectingPlace> place);
    void enter(std::unique_ptr<LobbyPlace> place);
    void enter(std::unique_ptr<OnlinePlace> place);

    OfflinePlace* offline_;
    Needs needs_;
    // While online, one of these (current_ it).
    std::unique_ptr<ConnectingPlace> connecting_;
    std::unique_ptr<LobbyPlace> lobby_;
    std::unique_ptr<OnlinePlace> game_;
    Place* current_;
    bool hosting_ = false;  // the last page online the title opened: HOST GAME
    bool done_ = false;
    bool failed_ = false;
};

}  // namespace lvr
