#include "lvr/menu.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <utility>

#include "lvr/render.hpp"
#include "lvr/session.hpp"

namespace lvr {
namespace {

using input::Action;

// (No line to resume: Escape, B or Start does that, the user.)
constexpr std::array<std::string_view, 5> kFirstLines{"OPTIONS", "KEYBOARD", "GAMEPAD", "GIVE UP LEVEL", "QUIT GAME"};
enum class Option : std::uint8_t { kSound, kMusic, kEffects, kScreen, kZoom, kMinimap, kLights, kBack };
constexpr std::size_t kOptions = 8;
constexpr std::array<std::string_view, Session::kZoomSteps> kZoomNames{"1X", "1.5X", "2X"};
constexpr std::size_t kControlsAfter = 2;  // after the actions: the defaults, and back

// The box's text, in the font's 8 x 8 characters: an action's name in this many, its keys beside it in the rest.
constexpr int kCharacter = 8;
constexpr int kLabelColumns = 15;
constexpr int kValueColumns = 21;
constexpr int kColumns = kLabelColumns + 1 + kValueColumns;
constexpr int kMostLines = 15;  // a longer page shows this many around the chosen line

std::string upper(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

std::string_view on_off(bool on) {
    return on ? "ON" : "OFF";
}

// A key's name as the menu shows it: in capitals, and a sign's spelled out (the font's - sits low, as a _ would).
std::string key_label(std::string_view key) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 9> kSigns{{
        {"-", "MINUS"},
        {"=", "EQUALS"},
        {",", "COMMA"},
        {".", "PERIOD"},
        {"/", "SLASH"},
        {"\\", "BACKSLASH"},
        {";", "SEMICOLON"},
        {"'", "QUOTE"},
        {"`", "GRAVE"},
    }};
    for (const auto& [sign, name] : kSigns) {
        if (key == sign) {
            return std::string{name};
        }
    }
    return upper(key);
}

// The names of what does an action, as many as fit, and how many more there are.
std::string listed(const std::vector<std::string>& bound, bool pad) {
    if (bound.empty()) {
        return "-";
    }
    std::string text;
    for (std::size_t i = 0; i < bound.size(); ++i) {
        const std::string name = pad ? std::string{input::pad_label(bound.at(i))} : key_label(bound.at(i));
        const std::string next = text.empty() ? name : std::format("{}, {}", text, name);
        const bool more = i + 1 < bound.size();
        if (std::cmp_greater(next.size(), more ? kValueColumns - 3 : kValueColumns)) {  // (room for " +N")
            return text.empty() ? next.substr(0, kValueColumns) : std::format("{} +{}", text, bound.size() - i);
        }
        text = next;
    }
    return text;
}

}  // namespace

Menu::Menu(Settings settings, Choices choices) : settings_{std::move(settings)}, choices_{std::move(choices)} {
    if (choices_.screens.empty()) {
        choices_.screens.emplace_back("none");
    }
}

std::vector<std::string_view> Menu::first_lines() const {
    std::vector<std::string_view> lines{kFirstLines.begin(), kFirstLines.end()};
    if (choices_.online) {
        lines.insert(lines.begin() + 3, "PLAYERS");  // (after the controls)
    }
    return lines;
}

std::size_t Menu::line_count() const {
    switch (page_) {
        case Page::kOptions:
            return kOptions;
        case Page::kKeyboard:
        case Page::kGamepad:
            return input::kActionCount + kControlsAfter;
        case Page::kPlayers:
            return players_.size() + 1;  // and back
        case Page::kFirst:
            break;
    }
    return first_lines().size();
}

std::string_view Menu::title() const noexcept {
    switch (page_) {
        case Page::kOptions:
            return "OPTIONS";
        case Page::kKeyboard:
            return "KEYBOARD";
        case Page::kGamepad:
            return "GAMEPAD";
        case Page::kPlayers:
            return "PLAYERS";
        case Page::kFirst:
            break;
    }
    return "MENU";
}

std::vector<Menu::Line> Menu::lines() const {
    std::vector<Line> lines;
    // A line: a choice alone, or with a value that left and right change.
    const auto line = [&lines](std::string_view label, std::string value = {}, bool changes = false) {
        Line& made = lines.emplace_back();
        made.label = label;
        made.value = std::move(value);
        made.changes = changes;
    };
    switch (page_) {
        case Page::kFirst:
            for (const std::string_view label : first_lines()) {
                line(label);
            }
            break;
        case Page::kPlayers:
            for (const PlayerLine& player : players_) {
                line(player.name, std::format("{:<7} {}", player.viking, player.latency));
            }
            line("BACK");
            break;
        case Page::kOptions: {
            const auto screen = settings_.screen == "none" ? std::string{"OFF"} : upper(settings_.screen);
            const auto zoom = static_cast<std::size_t>(std::clamp(settings_.zoom, 0, Session::kZoomSteps - 1));
            line("SOUND", settings_.modern_sound && choices_.has_modern ? "MODERN" : "CLASSIC", choices_.has_modern);
            lines.back().dim = !choices_.has_modern;  // (no SoundFont)
            line("MUSIC", {}, true);
            lines.back().slider = settings_.music;
            line("SOUND FX", {}, true);
            lines.back().slider = settings_.sound_fx;
            line("SCREEN", screen, choices_.screens.size() > 1);
            line("ZOOM", std::string{kZoomNames.at(zoom)}, true);
            line("MINIMAP", std::string{on_off(settings_.minimap)}, true);
            line("VISUAL FX", std::string{on_off(settings_.visual_fx)}, true);
            line("BACK");
            break;
        }
        case Page::kKeyboard:
        case Page::kGamepad: {
            const bool pad = page_ == Page::kGamepad;
            for (std::size_t action = 0; action < input::kActionCount; ++action) {
                line(input::label_of(static_cast<Action>(action)),
                     listed(pad ? settings_.controls.pad.at(action) : settings_.controls.keys.at(action), pad));
                lines.back().dim = pad && !has_pad_;  // (nothing to bind with)
            }
            line("DEFAULTS");
            lines.back().dim = pad && !has_pad_;
            line("BACK");
            break;
        }
    }
    return lines;
}

std::vector<std::string> Menu::note() const {
    const auto action = [this] { return input::label_of(static_cast<Action>(chosen())); };
    if (waiting_ == Waiting::kKey) {
        return {std::format("PRESS A KEY FOR {}", action()), "ESC LEAVES IT AS IT IS"};
    }
    if (waiting_ == Waiting::kPad) {
        return {std::format("PRESS A BUTTON FOR {}", action()), "START LEAVES IT AS IT IS"};
    }
    std::vector<std::string> note;
    if (!said_.empty()) {
        note.push_back(said_);
    } else if (page_ == Page::kGamepad) {
        note.emplace_back(has_pad_ ? "GAMEPAD FOUND" : "NO GAMEPAD CONNECTED");
    }
    switch (page_) {
        case Page::kFirst:
            note.emplace_back("ENTER/A CHOOSES, ESC/B RESUMES");
            break;
        case Page::kOptions:
            note.emplace_back("LEFT/RIGHT CHANGE, ESC/B BACK");
            break;
        case Page::kKeyboard:
            note.emplace_back("ENTER/A SET, DEL/Y CLEAR, ESC/B BACK");
            break;
        case Page::kGamepad:
            note.emplace_back("ENTER/A ADD, DEL/Y CLEAR, ESC/B BACK");
            break;
        case Page::kPlayers:
            note.emplace_back("ESC/B BACK");
            break;
    }
    return note;
}

bool Menu::note_flashes() const noexcept {
    return page_ == Page::kGamepad && !has_pad_ && said_.empty() && waiting_ == Waiting::kNothing;
}

void Menu::set_has_pad(bool has_pad) noexcept {
    has_pad_ = has_pad;
    if (!has_pad && waiting_ == Waiting::kPad) {  // (the one to bind with is gone)
        waiting_ = Waiting::kNothing;
    }
}

Menu::Asked Menu::press(Key key) {
    if (waiting_ != Waiting::kNothing) {
        return Asked::kNothing;
    }
    said_.clear();
    std::size_t& chosen = chosen_.at(static_cast<std::size_t>(page_));
    const std::size_t count = line_count();
    switch (key) {
        case Key::kUp:
            chosen = (chosen + count - 1) % count;
            break;
        case Key::kDown:
            chosen = (chosen + 1) % count;
            break;
        case Key::kLeft:
        case Key::kRight:
            if (page_ == Page::kOptions) {
                change(key == Key::kLeft ? -1 : 1);
            }
            break;
        case Key::kChoose:
            return choose();
        case Key::kBack:
            if (page_ == Page::kFirst) {
                return Asked::kResume;
            }
            page_ = Page::kFirst;
            break;
        case Key::kClear:
            if ((page_ == Page::kKeyboard || (page_ == Page::kGamepad && has_pad_)) && chosen < input::kActionCount) {
                (page_ == Page::kGamepad ? settings_.controls.pad : settings_.controls.keys).at(chosen).clear();
            }
            break;
    }
    return Asked::kNothing;
}

Menu::Asked Menu::choose() {
    const std::size_t chosen = this->chosen();
    switch (page_) {
        case Page::kFirst: {
            const std::string_view label = first_lines().at(chosen);
            if (label == "GIVE UP LEVEL") {
                return Asked::kGiveUp;
            }
            if (label == "QUIT GAME") {
                return Asked::kQuit;
            }
            page_ = label == "OPTIONS"    ? Page::kOptions
                    : label == "KEYBOARD" ? Page::kKeyboard
                    : label == "GAMEPAD"  ? Page::kGamepad
                                          : Page::kPlayers;
            break;
        }
        case Page::kPlayers:
            if (chosen == players_.size()) {
                page_ = Page::kFirst;
            }
            break;
        case Page::kOptions:
            if (chosen == static_cast<std::size_t>(Option::kBack)) {
                page_ = Page::kFirst;
            } else {
                change(1);
            }
            break;
        case Page::kKeyboard:
        case Page::kGamepad: {
            const bool pad = page_ == Page::kGamepad;
            if (pad && !has_pad_ && chosen <= input::kActionCount) {
                break;  // (without a gamepad its buttons are left as they are)
            }
            if (chosen < input::kActionCount) {
                waiting_ = pad ? Waiting::kPad : Waiting::kKey;
            } else if (chosen == input::kActionCount) {  // the defaults
                const input::Controls defaults = input::remaster_controls();
                (pad ? settings_.controls.pad : settings_.controls.keys) = pad ? defaults.pad : defaults.keys;
            } else {
                page_ = Page::kFirst;
            }
            break;
        }
    }
    return Asked::kNothing;
}

void Menu::change(int by) {
    switch (static_cast<Option>(chosen())) {
        case Option::kSound:
            settings_.modern_sound = choices_.has_modern && !settings_.modern_sound;
            break;
        case Option::kMusic:
            settings_.music = std::clamp(settings_.music + by, 0, kLoudest);
            break;
        case Option::kEffects:
            settings_.sound_fx = std::clamp(settings_.sound_fx + by, 0, kLoudest);
            break;
        case Option::kScreen: {
            const auto found = std::ranges::find(choices_.screens, settings_.screen);
            const auto at = found == choices_.screens.end() ? 0 : found - choices_.screens.begin();
            const auto count = static_cast<std::ptrdiff_t>(choices_.screens.size());
            settings_.screen = choices_.screens.at(static_cast<std::size_t>((at + by + count) % count));
            break;
        }
        case Option::kZoom:
            settings_.zoom = std::clamp(settings_.zoom + by, 0, Session::kZoomSteps - 1);
            break;
        case Option::kMinimap:
            settings_.minimap = !settings_.minimap;
            break;
        case Option::kLights:
            settings_.visual_fx = !settings_.visual_fx;
            break;
        case Option::kBack:
            break;
    }
}

void Menu::take_key(std::string_view key) {
    if (waiting_ != Waiting::kKey) {
        return;
    }
    waiting_ = Waiting::kNothing;
    const auto action = static_cast<Action>(chosen());
    const input::Actions had = input::key_actions(settings_.controls, key) & ~input::bit(action);
    if (!input::bind_key(settings_.controls, action, key)) {
        said_ = std::format("{} CAN NOT BE CHANGED", key_label(key));
        return;
    }
    for (std::size_t other = 0; other < input::kActionCount; ++other) {
        if ((had & input::bit(static_cast<Action>(other))) != 0) {
            said_ = std::format("{} NO LONGER DOES {}", key_label(key), input::label_of(static_cast<Action>(other)));
        }
    }
}

void Menu::take_pad(std::string_view input) {
    if (waiting_ != Waiting::kPad) {
        return;
    }
    waiting_ = Waiting::kNothing;
    const auto action = static_cast<Action>(chosen());
    const input::Actions had = input::pad_actions(settings_.controls, input) & ~input::bit(action);
    if (!input::bind_pad(settings_.controls, action, input)) {
        said_ = std::format("{} CAN NOT BE CHANGED", input::pad_label(input));
        return;
    }
    for (std::size_t other = 0; other < input::kActionCount; ++other) {
        if ((had & input::bit(static_cast<Action>(other))) != 0) {
            said_ = std::format("{} NO LONGER DOES {}", input::pad_label(input),
                                input::label_of(static_cast<Action>(other)));
        }
    }
}

void Menu::stop_waiting() noexcept {
    waiting_ = Waiting::kNothing;
}

namespace {

// The colours, as the rewind's bar's (gpu_gauge.frag): the game's white, a light grey, and the dark it is on.
struct Colour {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};
constexpr std::uint8_t kSeeThrough = 235;  // 92%: the box (the text and the frame are solid)
constexpr Colour kWhite{251, 251, 251, 255};
constexpr Colour kGrey{170, 170, 170, 255};
constexpr Colour kDimmed{96, 96, 96, 255};
constexpr Colour kDark{4, 4, 4, kSeeThrough};           // black (the user: not the bar's blue-black)
constexpr Colour kChosenBand{40, 40, 40, kSeeThrough};  // behind the chosen line

class Canvas {
  public:
    Canvas() : pixels_(std::size_t{kScreenWidth} * kScreenHeight * 4) {}
    void put(int x, int y, Colour c) {
        if (x < 0 || y < 0 || x >= kScreenWidth || y >= kScreenHeight) {
            return;
        }
        const std::size_t at = ((static_cast<std::size_t>(y) * kScreenWidth) + static_cast<std::size_t>(x)) * 4;
        pixels_.at(at) = c.r;
        pixels_.at(at + 1) = c.g;
        pixels_.at(at + 2) = c.b;
        pixels_.at(at + 3) = c.a;
    }
    void fill(int x, int y, int width, int height, Colour c) {
        for (int row = y; row < y + height; ++row) {
            for (int column = x; column < x + width; ++column) {
                put(column, row, c);
            }
        }
    }
    // A box with a frame a pixel wide and two pixels cut off each corner, as the rewind's bar's.
    void framed(int x, int y, int width, int height, Colour inside, Colour frame) {
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                const int across = std::min(column, width - 1 - column);
                const int down = std::min(row, height - 1 - row);
                if (across + down < 2) {
                    continue;  // cut off
                }
                const bool edge = across == 0 || down == 0 || across + down == 2;
                put(x + column, y + row, edge ? frame : inside);
            }
        }
    }
    // A text in the font: a character's letter in `c`, its shadow dark, and nothing of the box it is drawn on (the
    // font's three colours, as the text boxes give them their own).
    void text(int x, int y, std::string_view text, Colour c, const std::vector<assets::IndexedImage>& font) {
        constexpr std::uint8_t kFirstGlyph = 0x10;
        constexpr std::uint8_t kShadow = 1;
        constexpr std::uint8_t kLetter = 2;
        constexpr Colour kShadowColour{kDark.r, kDark.g, kDark.b, 255};
        for (const char each : text) {
            const auto character = static_cast<std::uint8_t>(each);
            if (character >= kFirstGlyph && std::size_t{character} - kFirstGlyph < font.size()) {
                const assets::IndexedImage& glyph = font.at(std::size_t{character} - kFirstGlyph);
                for (std::size_t gy = 0; gy < glyph.height; ++gy) {
                    for (std::size_t gx = 0; gx < glyph.width; ++gx) {
                        if (const std::uint8_t colour = glyph.at(gx, gy); colour == kLetter || colour == kShadow) {
                            put(x + static_cast<int>(gx), y + static_cast<int>(gy),
                                colour == kLetter ? c : kShadowColour);
                        }
                    }
                }
            }
            x += kCharacter;
        }
    }
    // A small triangle, its point at (x, y): pointing left, right, up or down, 3 pixels long.
    enum class Pointing : std::uint8_t { kLeft, kRight, kUp, kDown };
    void arrow(int x, int y, Pointing pointing, Colour c) {
        for (int step = 0; step < 3; ++step) {
            for (int side = -step; side <= step; ++side) {
                switch (pointing) {
                    case Pointing::kLeft:
                        put(x + step, y + side, c);
                        break;
                    case Pointing::kRight:
                        put(x - step, y + side, c);
                        break;
                    case Pointing::kUp:
                        put(x + side, y + step, c);
                        break;
                    case Pointing::kDown:
                        put(x + side, y - step, c);
                        break;
                }
            }
        }
    }
    std::vector<std::uint8_t> done() { return std::move(pixels_); }

  private:
    std::vector<std::uint8_t> pixels_;
};

}  // namespace

std::vector<std::uint8_t> coop_picture(const CoopOverlay& overlay, const std::vector<assets::IndexedImage>& font) {
    constexpr int kNameLine = 9;
    Canvas canvas;
    for (const CoopOverlay::Label& label : overlay.labels) {
        for (std::size_t i = 0; i < label.names.size(); ++i) {
            const std::string& name = label.names.at(i);
            const int width = static_cast<int>(name.size()) * kCharacter;
            canvas.text(label.x - (width / 2), label.y + (static_cast<int>(i) * kNameLine), name,
                        i == 0 ? kWhite : kGrey, font);
        }
    }
    if (!overlay.waiting.empty()) {
        constexpr int kPadding = 6;
        const int width = (static_cast<int>(overlay.waiting.size()) * kCharacter) + (2 * kPadding);
        const int height = kCharacter + (2 * kPadding) - 2;
        const int x = (kScreenWidth - width) / 2;
        constexpr int kTop = 8;
        canvas.framed(x, kTop, width, height, kDark, kWhite);
        canvas.text(x + kPadding, kTop + kPadding - 1, overlay.waiting, kWhite, font);
    }
    return canvas.done();
}

std::vector<std::uint8_t> menu_picture(const Menu& menu, const std::vector<assets::IndexedImage>& font,
                                       bool flash_shown) {
    constexpr int kPadding = 8;  // inside the frame, around the text
    constexpr int kLine = 10;    // a line of text with a pixel above and below it
    constexpr int kNoteLine = 9;
    const std::vector<Menu::Line> lines = menu.lines();
    const std::vector<std::string> note = menu.note();
    const int shown = std::min(static_cast<int>(lines.size()), kMostLines);
    const int width = (kColumns * kCharacter) + (2 * kPadding);
    const int height = 6 + kCharacter + 6 + (shown * kLine) + 6 + (static_cast<int>(note.size()) * kNoteLine) + 4;
    const int x = (kScreenWidth - width) / 2;
    const int y = (kScreenHeight - height) / 2;
    Canvas canvas;
    canvas.framed(x, y, width, height, kDark, kWhite);
    const int text_x = x + kPadding;
    const std::string_view title = menu.title();
    canvas.text(x + ((width - (static_cast<int>(title.size()) * kCharacter)) / 2), y + 6, title, kWhite, font);
    canvas.fill(text_x, y + 6 + kCharacter + 2, kColumns * kCharacter, 1, kDimmed);
    // The lines around the chosen one, as many as the box holds.
    const int chosen = static_cast<int>(menu.chosen());
    const int first = std::clamp(chosen - (shown / 2), 0, static_cast<int>(lines.size()) - shown);
    const int lines_y = y + 6 + kCharacter + 6;
    for (int i = 0; i < shown; ++i) {
        const Menu::Line& line = lines.at(static_cast<std::size_t>(first) + static_cast<std::size_t>(i));
        const bool is_chosen = first + i == chosen;
        const int top = lines_y + (i * kLine);
        if (is_chosen) {
            canvas.fill(text_x - 4, top, (kColumns * kCharacter) + 8, kLine, kChosenBand);
        }
        const Colour ink = line.dim ? kDimmed : is_chosen ? kWhite : kGrey;
        canvas.text(text_x, top + 1, line.label, ink, font);
        int value_x = text_x + ((kLabelColumns + 1) * kCharacter);
        if (line.changes && is_chosen) {
            canvas.arrow(value_x - 7, top + 4, Canvas::Pointing::kLeft, ink);
        }
        if (line.slider) {
            // A volume: a bar of kLoudest steps in the rewind bar's look, and its number after it.
            constexpr int kStep = 5;
            const int bar_width = (kLoudest * kStep) + 3;
            canvas.framed(value_x, top + 1, bar_width, 8, kDark, ink);
            for (int step = 0; step < *line.slider; ++step) {
                canvas.fill(value_x + 2 + (step * kStep), top + 3, kStep - 1, 4, ink);
            }
            value_x += bar_width + 4;
            canvas.text(value_x, top + 1, std::format("{}", *line.slider), ink, font);
            value_x += 2 * kCharacter;
        } else if (!line.value.empty()) {
            canvas.text(value_x, top + 1, line.value, ink, font);
            value_x += static_cast<int>(line.value.size()) * kCharacter;
        }
        if (line.changes && is_chosen) {
            canvas.arrow(value_x + 6, top + 4, Canvas::Pointing::kRight, ink);
        }
    }
    if (first > 0) {  // more above, more below
        canvas.arrow(x + width - 6, lines_y + 1, Canvas::Pointing::kUp, kGrey);
    }
    if (first + shown < static_cast<int>(lines.size())) {
        canvas.arrow(x + width - 6, lines_y + (shown * kLine) - 2, Canvas::Pointing::kDown, kGrey);
    }
    const int note_y = lines_y + (shown * kLine) + 6;
    for (std::size_t i = 0; i < note.size(); ++i) {
        if (i == 0 && menu.note_flashes() && !flash_shown) {
            continue;
        }
        canvas.text(text_x, note_y + (static_cast<int>(i) * kNoteLine), note.at(i).substr(0, kColumns), kGrey, font);
    }
    return canvas.done();
}

}  // namespace lvr
