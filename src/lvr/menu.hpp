#pragma once

// Remaster rules' menu (ADR-0011): opened with Escape or a gamepad's Start in a level being played, laid over the
// game, which holds while it is open (no step runs: the game never sees it). Its pages: the first (resume, the
// options, the keyboard's and the gamepad's controls, giving up the level, quitting), the options (the sound and
// its two volumes, the screen's look, the zoom, the minimap, the visual effects), and the controls, where an action is
// bound to the next key or button pressed. And at the title, under Remaster rules offline, HOST GAME and JOIN GAME
// (ADR-0012): opened by the title's own menu, their lines typed into, closed again by going back; and the LOBBY of a
// game online, where everyone waits for the host to start it (the host's in a game online: RETURN TO LOBBY). It works
// on a copy of the player's settings (profile.hpp), which the front end takes into use as they change. Nothing here
// knows about windows or devices: the front end gives it the keys (Key) and the names of what is pressed, and lays its
// picture (menu_picture) over the screen.

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/graphics.hpp"
#include "lvr/profile.hpp"

namespace lvr {

class Menu {
  public:
    // What there is to choose from besides the settings: a Modern sound (only with a SoundFont), and the screen's
    // looks by name, in the order the menu goes through them.
    struct Choices {
        bool has_modern = false;
        std::vector<std::string> screens{"none"};
        bool online = false;   // co-op online (ADR-0012): the players and their latency, under the first page's lines
        bool hosting = false;  // online, this machine hosts: it drives the game (GIVE UP LEVEL is its only)
        bool lobby = false;    // online, from the title's lobby: the host's RETURN TO LOBBY
        // Every level's password, in order: what HOST GAME's is checked against (the level the host starts in).
        std::vector<std::string> passwords;
    };
    Menu(Settings settings, Choices choices);

    // The menu's own keys, fixed (Settings::controls don't change them): up and down choose a line, left and right
    // change its value, choose takes it, back goes back a page (from the first: resumes), clear takes the keys or
    // buttons off an action.
    enum class Key : std::uint8_t { kUp, kDown, kLeft, kRight, kChoose, kBack, kClear };
    // What a key asks of the front end, apart from what it changed in the settings.
    enum class Asked : std::uint8_t {
        kNothing,
        kResume,  // the menu is closed, the game goes on
        kGiveUp,  // closed, and the original's pause box is opened (where the level is given up)
        kQuit,    // closed, and the original's quit box is opened
        kHost,    // closed: host a game online, as the settings say, in the level of `level()`
        kJoin,    // closed: join the game at the settings' address
        kStart,   // the lobby's host: the game starts, in the level of `level()` (the menu stays until it does)
        kLeave,   // the lobby left
        kLobby,   // the host in a game online: everyone back to the lobby
        kCancel,  // the lobby's host: the countdown to the start stopped
    };
    Asked press(Key key);

    // Waiting for the key (or the gamepad's input) to bind to an action: the front end gives the next one pressed.
    // A fixed one (input.hpp) is refused and says so; Escape (or Start) gives up waiting (stop_waiting).
    // Or the line chosen is being typed into (kText): the front end gives what is typed (take_text), and Backspace
    // (erase); Enter or Escape ends it (stop_waiting).
    enum class Waiting : std::uint8_t { kNothing, kKey, kPad, kText };
    Waiting waiting() const noexcept { return waiting_; }
    void take_key(std::string_view key);
    void take_pad(std::string_view input);
    void stop_waiting() noexcept;
    void take_text(std::string_view typed);
    void erase();
    // What the menu says in its note until the next key (a game online that couldn't be joined, and why).
    void tell(std::string said) { said_ = std::move(said); }
    // JOIN GAME's JOIN under way (the connection being made): a mark turning beside it, at this phase; none: not.
    // Meanwhile the menu takes no key: neither a line nor back.
    void set_joining(std::optional<int> phase) { joining_ = phase; }
    // Or what it came to (it couldn't be made: why), in the mark's place; empty: nothing.
    void set_join_said(std::string said) { join_said_ = std::move(said); }
    // Opens on HOST GAME or JOIN GAME (the title's menu asked for it): going back from it closes the menu.
    void open_host();
    void open_join();
    // The title's OPTIONS: the first page without GIVE UP LEVEL and QUIT TO DOS, over the title; back closes it.
    void open_options();
    // Opens on the lobby: the host's has the password to start with and START GAME; going back leaves it.
    void open_lobby(bool hosting);
    bool in_lobby() const noexcept { return page_ == Page::kLobby; }
    // HOST GAME, JOIN GAME and the lobby: pages of the title's (a menu of their own under its logo, not laid over a
    // game going on).
    bool of_title() const noexcept {
        return from_title_ || page_ == Page::kHost || page_ == Page::kJoin || page_ == Page::kLobby;
    }
    // What the lobby's game starts with, as the front end words it (NEW GAME, or a level and its password).
    void set_start(std::string start) { start_ = std::move(start); }
    const std::string& start() const noexcept { return start_; }
    // The host has started it: the seconds until it starts (its title says so; the host's START GAME cancels it).
    void set_countdown(int seconds) {
        countdown_ = seconds;
        title_ = std::format("STARTING IN {}", seconds);
    }
    int countdown() const noexcept { return countdown_; }
    // The level the host starts in, by its password (Asked::kHost, kStart): its scene's slot; -1: no password, a
    // new game (from the intro, as the title's NEW GAME).
    int level() const noexcept { return level_; }
    // The level the password typed so far names, as level() would be: none while it names none.
    std::optional<int> level_typed() const;

    const Settings& settings() const noexcept { return settings_; }
    // Whether a gamepad is there, as it is plugged in and out: the gamepad's page says so ("found", or flashing
    // that none is), and without one its buttons can't be bound (or cleared).
    void set_has_pad(bool has_pad) noexcept;
    // Online: the players, as the front end has them now (their latency changes while the menu is open), shown under
    // the first page's lines.
    struct PlayerLine {
        std::string name;
        // His round trips to the host ("23 MS"; "-" for the host, or where it isn't known): the network's and the
        // game's.
        std::string network;
        std::string engine;
        bool operator==(const PlayerLine&) const = default;
    };
    void set_players(std::vector<PlayerLine> players) { players_ = std::move(players); }
    const std::vector<PlayerLine>& players() const noexcept { return players_; }
    bool shows_players() const noexcept {
        return ((page_ == Page::kFirst && choices_.online) || page_ == Page::kLobby) && !players_.empty();
    }

    // What is shown, for menu_picture (and a test): the page's title, its lines, the one chosen, and a note.
    struct Line {
        std::string label;
        std::string value;           // beside it (a setting's value, an action's keys): none for a choice
        std::optional<int> slider;   // a volume: shown as a bar of kLoudest
        bool changes = false;        // left and right change the value
        bool dim = false;            // nothing can be chosen there
        bool typing = false;         // being typed into: a block after its value, where the next letter goes
        std::optional<int> turning;  // a mark turning in its value's place (| / - \\ by its phase): it is under way
    };
    std::string_view title() const noexcept;
    std::vector<Line> lines() const;
    std::size_t chosen() const { return chosen_.at(static_cast<std::size_t>(page_)); }
    std::vector<std::string> note() const;  // a line or two at the bottom: what the keys do, or what happened
    bool note_flashes() const noexcept;     // its first line comes and goes (no gamepad)

  private:
    enum class Page : std::uint8_t { kFirst, kOptions, kKeyboard, kGamepad, kHost, kJoin, kLobby };
    static constexpr std::size_t kPages = 7;
    // The first page's lines, as there are now.
    enum class First : std::uint8_t { kOptions, kKeyboard, kGamepad, kLobby, kGiveUp, kQuit };
    std::vector<First> first_lines() const;
    // What a line typed into holds.
    enum class Field : std::uint8_t { kNone, kName, kPort, kPassword, kAddress };
    Field field_of(std::size_t line) const noexcept;
    // The text a line of HOST GAME or JOIN GAME holds; none: it isn't one.
    std::string* text_of(std::size_t line);
    const std::string* text_of(std::size_t line) const;
    std::size_t line_count() const;
    void change(int by);  // the chosen option's value
    Asked choose();

    Settings settings_;
    Choices choices_;
    Page page_ = Page::kFirst;
    bool from_title_ = false;  // opened by the title's OPTIONS
    std::array<std::size_t, kPages> chosen_{};
    Waiting waiting_ = Waiting::kNothing;
    std::string said_;  // what the last key did, if it says something (a key that can't be bound)
    bool has_pad_ = false;
    std::vector<PlayerLine> players_;
    std::string port_;  // HOST GAME's port, as typed (into the settings as a number)
    int level_ = -1;
    bool lobby_host_ = false;
    std::optional<int> joining_;
    std::string join_said_;
    std::string start_;
    int countdown_ = 0;
    std::string title_;  // (a title made of the countdown, for title())
};

// The menu's picture, the original's screen's size (kScreenWidth x kScreenHeight, render.hpp): red, green, blue and
// alpha a pixel, clear around its box. A box in the rewind bar's look (black, hardly see-through, a white frame with
// its corners cut off), its text in the game's own font (`font`: SharedGraphics::font), each pixel one colour. A page
// longer than the box shows the part around the chosen line, with a mark where there is more. `flash_shown`: whether
// a note that flashes (note_flashes) is there in this one.
std::vector<std::uint8_t> menu_picture(const Menu& menu, const std::vector<assets::IndexedImage>& font,
                                       bool flash_shown = true);

// Co-op online (ADR-0012): what is laid over the screen for the players, in the menu's look: while the game waits
// for a player's input, a box at the screen's top saying for whom.
struct CoopOverlay {
    std::string waiting;  // none: empty
    bool operator==(const CoopOverlay&) const = default;
};
std::vector<std::uint8_t> coop_picture(const CoopOverlay& overlay, const std::vector<assets::IndexedImage>& font);

}  // namespace lvr
