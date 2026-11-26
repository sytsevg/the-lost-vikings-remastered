#include "lvr/handler.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "lvr/names.hpp"

namespace lvr {

using input::Action;
using input::bit;

input::Actions InputHandler::key_actions(std::string_view key) const {
    input::Actions actions = input::key_actions(controls_, key);
    if (remaster_ && key == "Escape") {  // (Escape opens the menu in a level being played; elsewhere the pause)
        actions |= bit(Action::kPause);
    }
    return actions;
}

// Remaster rules' own actions of a key or button pressed, taken where they act (Session::takes_play_keys): the zoom,
// the minimap, the quicksave. Elsewhere a key is the game's like any other: nothing pressed in a scene or a menu is
// kept for later. True if one took it.
bool InputHandler::act(input::Actions actions, Session& session) const {
    bool taken = false;
    const auto has = [actions](Action action) { return (actions & bit(action)) != 0; };
    taken = (has(Action::kZoomOut) && session.zoom(1)) || taken;
    taken = (has(Action::kZoomIn) && session.zoom(-1)) || taken;
    taken = (has(Action::kMinimap) && session.toggle_minimap()) || taken;
    taken = (has(Action::kQuicksave) && session.quicksave()) || taken;
    taken = (has(Action::kQuickload) && session.quickload()) || taken;
    return taken;
}

// Any key ends a demo (the original); under Remaster rules a scene that plays by itself (the intro and the night
// after it, the title's demos, the ending) only an action key, Enter or Escape (the user: not a key pressed by the
// way).
bool InputHandler::skips_demo(std::uint16_t keys) const {
    constexpr auto kSkips = static_cast<std::uint16_t>(key::kAction | key::kEnter);
    return !remaster_ || (unsigned{keys} & unsigned{kSkips}) != 0;
}

InputHandler::Asks InputHandler::pressed(std::string_view key, bool repeat, bool alt, Session& session) {
    if (alt && (key == "S" || key == "M")) {
        // Alt+S, Alt+M: the sound switches, acted on after the next step (a repeat counts again). As the original's
        // handler, S with Alt held isn't a typed character.
        (key == "S" ? sound_keys_.effects : sound_keys_.music) = true;
        if (key == "M") {
            session.key_pressed(input::typed_character(key));
        }
        return Asks::kNothing;
    }
    if (remaster_ && !repeat && key == "Escape" && session.takes_play_keys()) {
        return Asks::kMenu;
    }
    escaped_ = escaped_ || (key == "Escape" && !repeat);
    choose_ = choose_ || (remaster_ && !repeat && (key == "Return" || key == "Keypad Enter"));
    // A press, or the keyboard's own repeat of a held key, which reaches the original's handler as a press too.
    // Remaster's own (not repeated) go first; one they take isn't the game's.
    const input::Actions actions = key_actions(key);
    session.key_pressed(remaster_ && key == "Escape" ? input::kTypedEscape : input::typed_character(key));
    if (!(remaster_ && !repeat && act(actions, session))) {
        keyboard_ |= input::game_bits(actions);
    }
    if (session.demo() && skips_demo(input::game_bits(actions))) {  // the keyboard handler: a key during a demo
        keyboard_ = 0xFFFF;
    }
    return Asks::kNothing;
}

void InputHandler::released(std::string_view key) {
    keyboard_ &= ~unsigned{input::game_bits(key_actions(key))};
}

void InputHandler::read_pads(const input::PadReading& reading) {
    now_ = reading;
    if (std::exchange(hush_, false)) {
        quiet_ = now_;
    }
    for (std::size_t i = 0; i < now_.size(); ++i) {
        quiet_.at(i) = now_.at(i) > 0.5F ? quiet_.at(i) : 0;
        now_.at(i) = quiet_.at(i) > 0.5F ? 0 : now_.at(i);
    }
}

InputHandler::Asks InputHandler::act_on_pads(Session& session) {
    if (!remaster_) {
        return Asks::kNothing;
    }
    const std::size_t start_button = input::pad_input_index("start").value_or(0);
    if (now_.at(start_button) > 0.5F && before_.at(start_button) <= 0.5F && session.takes_play_keys()) {
        return Asks::kMenu;
    }
    const input::Actions pushed =
        input::read_pad(controls_, now_, true).held & ~input::read_pad(controls_, before_, true).held;
    (void)act(pushed & ~input::kGameActions, session);
    return Asks::kNothing;
}

Tick InputHandler::tick(const std::function<bool(std::string_view)>& held, Session& session) {
    const input::PadActions from_pads = input::read_pad(controls_, now_, remaster_);
    std::uint16_t pad = input::game_bits(from_pads.held);
    const std::size_t start_button = input::pad_input_index("start").value_or(0);
    if (remaster_ && now_.at(start_button) > 0.5F && !session.takes_play_keys()) {
        pad |= input::game_bits(bit(Action::kPause));  // Start: the original's pause, as it was
    }
    if (pad != 0 && session.demo() && skips_demo(pad)) {  // joystick input during a demo
        pad = 0xFFFF;
    }
    const auto any_held = [&](Action action) {
        return std::ranges::any_of(controls_.keys.at(static_cast<std::size_t>(action)),
                                   [&](const std::string& name) { return held(name); });
    };
    const bool alt = held("Left Alt") || held("Right Alt");
    // Remaster rules: back in time while it is held; walking and climbing as fast as a stick is pushed (a key: all
    // the way).
    session.rewind(remaster_ && (any_held(Action::kRewind) || (from_pads.held & bit(Action::kRewind)) != 0));
    const auto part = [&](Action one, Action other, float stick) {  // a key: all of it
        return static_cast<std::uint8_t>(
            (keyboard_ & input::game_bits(bit(one) | bit(other))) != 0
                ? kFullPace
                : std::clamp(static_cast<int>(std::ceil(stick * kFullPace)), 1, int{kFullPace}));
    };
    Tick tick{.keys = static_cast<std::uint16_t>(keyboard_ | pad),
              .menu_keys = {.yes = held("Y"),
                            .no = held("N"),
                            .quit = held("F10") || (alt && (held("X") || held("Q"))) || quit_box_,
                            .effects = sound_keys_.effects,
                            .music = sound_keys_.music,
                            .previous_level = held("F5"),
                            .next_level = held("F6"),
                            .choose = choose_},
              .pace = Pace{.across = part(Action::kLeft, Action::kRight, from_pads.walk),
                           .up_down = part(Action::kUp, Action::kDown, from_pads.climb)},
              .escaped = std::exchange(escaped_, false)};
    // The menu's GIVE UP LEVEL: kept until a step takes this machine's input (online it can't be given while the
    // game waits for the others' or lets them catch up).
    if (give_up_) {
        tick.keys |= input::game_bits(bit(Action::kPause));  // the original's pause box
    }
    return tick;
}

void InputHandler::stepped(bool took_input, const Session& session) {
    if (took_input) {
        give_up_ = quit_box_ = choose_ = false;
    }
    sound_keys_.effects = session.menu_keys().effects;
    sound_keys_.music = session.menu_keys().music;
    if (session.scene_loaded()) {
        keyboard_ = 0;  // as the original's level start: nothing is held until it is pressed again
    }
}

}  // namespace lvr
