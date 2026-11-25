#pragma once

// Remaster rules' menu (ADR-0011): opened with Escape or a gamepad's Start in a level being played, laid over the
// game, which holds while it is open (no step runs: the game never sees it). Its pages: the first (resume, the
// options, the keyboard's and the gamepad's controls, giving up the level, quitting), the options (the sound and
// its two volumes, the screen's look, the zoom, the minimap, the visual effects), and the controls, where an action is
// bound to the next key or button pressed. It works on a copy of the player's settings (profile.hpp), which the
// front end takes into use as they change. Nothing here knows about windows or devices: the front end gives it the
// keys (Key) and the names of what is pressed, and lays its picture (menu_picture) over the screen.

#include <array>
#include <cstddef>
#include <cstdint>
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
        bool online = false;  // co-op online (ADR-0012): a page of the players, and their latency
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
    };
    Asked press(Key key);

    // Waiting for the key (or the gamepad's input) to bind to an action: the front end gives the next one pressed.
    // A fixed one (input.hpp) is refused and says so; Escape (or Start) gives up waiting (stop_waiting).
    enum class Waiting : std::uint8_t { kNothing, kKey, kPad };
    Waiting waiting() const noexcept { return waiting_; }
    void take_key(std::string_view key);
    void take_pad(std::string_view input);
    void stop_waiting() noexcept;

    const Settings& settings() const noexcept { return settings_; }
    // Whether a gamepad is there, as it is plugged in and out: the gamepad's page says so ("found", or flashing
    // that none is), and without one its buttons can't be bound (or cleared).
    void set_has_pad(bool has_pad) noexcept;
    // Online: the players, as the front end has them now (their latency changes while the menu is open).
    struct PlayerLine {
        std::string name;
        std::string viking;   // the Viking he plays or watches
        std::string latency;  // his round trip ("23 MS"), "YOU", or "-" where it isn't known
        bool operator==(const PlayerLine&) const = default;
    };
    void set_players(std::vector<PlayerLine> players) { players_ = std::move(players); }
    const std::vector<PlayerLine>& players() const noexcept { return players_; }

    // What is shown, for menu_picture (and a test): the page's title, its lines, the one chosen, and a note.
    struct Line {
        std::string label;
        std::string value;          // beside it (a setting's value, an action's keys): none for a choice
        std::optional<int> slider;  // a volume: shown as a bar of kLoudest
        bool changes = false;       // left and right change the value
        bool dim = false;           // nothing can be chosen there
    };
    std::string_view title() const noexcept;
    std::vector<Line> lines() const;
    std::size_t chosen() const { return chosen_.at(static_cast<std::size_t>(page_)); }
    std::vector<std::string> note() const;  // a line or two at the bottom: what the keys do, or what happened
    bool note_flashes() const noexcept;     // its first line comes and goes (no gamepad)

  private:
    enum class Page : std::uint8_t { kFirst, kOptions, kKeyboard, kGamepad, kPlayers };
    static constexpr std::size_t kPages = 5;
    std::vector<std::string_view> first_lines() const;
    std::size_t line_count() const;
    void change(int by);  // the chosen option's value
    Asked choose();

    Settings settings_;
    Choices choices_;
    Page page_ = Page::kFirst;
    std::array<std::size_t, kPages> chosen_{};
    Waiting waiting_ = Waiting::kNothing;
    std::string said_;  // what the last key did, if it says something (a key that can't be bound)
    bool has_pad_ = false;
    std::vector<PlayerLine> players_;
};

// The menu's picture, the original's screen's size (kScreenWidth x kScreenHeight, render.hpp): red, green, blue and
// alpha a pixel, clear around its box. A box in the rewind bar's look (black, hardly see-through, a white frame with
// its corners cut off), its text in the game's own font (`font`: SharedGraphics::font), each pixel one colour. A page
// longer than the box shows the part around the chosen line, with a mark where there is more. `flash_shown`: whether
// a note that flashes (note_flashes) is there in this one.
std::vector<std::uint8_t> menu_picture(const Menu& menu, const std::vector<assets::IndexedImage>& font,
                                       bool flash_shown = true);

// Co-op online (ADR-0012): what is laid over the screen for the players, in the menu's look. Under each Viking the
// names of his player (white) and of those watching him (grey), at a point of the screen; and while the game waits
// for a player's input, a box at the screen's top saying for whom.
struct CoopOverlay {
    struct Label {
        int x = 0;  // the middle of the names, on the original's screen (kScreenWidth x kScreenHeight)
        int y = 0;  // their top
        std::vector<std::string> names;  // his player's first, then his viewers'
        bool operator==(const Label&) const = default;
    };
    std::vector<Label> labels;
    std::string waiting;  // none: empty
    bool operator==(const CoopOverlay&) const = default;
};
std::vector<std::uint8_t> coop_picture(const CoopOverlay& overlay, const std::vector<assets::IndexedImage>& font);

}  // namespace lvr
