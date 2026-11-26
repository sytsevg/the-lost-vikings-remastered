#pragma once

// The player's input between steps, as the window gives it (docs/original/input.md): the keyboard word as the
// original's handler keeps it, the gamepads, Remaster rules' own keys and buttons acted on where they act, the menu's
// asks of the original's boxes; and at a step, its input (Tick). Keys by their names, as input::Controls names them.
// Nothing here knows windows or devices: the window's loop drives it, and so do --dump and the tests.

#include <cstdint>
#include <functional>
#include <string_view>

#include "input/input.hpp"
#include "lvr/front.hpp"
#include "lvr/session.hpp"

namespace lvr {

class InputHandler {
  public:
    InputHandler(input::Controls controls, bool remaster) : controls_{std::move(controls)}, remaster_{remaster} {}

    // The menu changed the controls.
    void set_controls(input::Controls controls) { controls_ = std::move(controls); }
    const input::Controls& controls() const noexcept { return controls_; }

    // What a key or a button asks of the window besides the game's keys.
    enum class Asks : std::uint8_t { kNothing, kMenu };
    // A key pressed, with Alt held or not; `repeat`: the keyboard's own repeat of a held key, which reaches the
    // original's handler as a press too (it types the character again, and sets the key's bits again if a scene's
    // load cleared them). Alt+S and Alt+M: the sound's switches, for the next step. Under Remaster rules Escape opens
    // the menu in a level being played, and Remaster's own keys (zoom, minimap, quicksave) act there; elsewhere a key
    // is the game's like any other. A key during a demo ends it (under Remaster rules an action key, Enter or Escape
    // only).
    Asks pressed(std::string_view key, bool repeat, bool alt, Session& session);
    // A key let go: its bits cleared, even if another key with them is held (as the original's handler).
    void released(std::string_view key);

    // The gamepads as this pass reads them: what was held as the menu closed is quiet until it is let go.
    void read_pads(const input::PadReading& reading);
    const input::PadReading& pads() const noexcept { return now_; }
    const input::PadReading& pads_before() const noexcept { return before_; }  // (the last pass's)
    // With the menu closed (Remaster rules): Start opens it in a level being played; Remaster's own buttons act as
    // they are pushed.
    Asks act_on_pads(Session& session);
    // The pass is over: what the gamepads hold now is what they held before the next.
    void pads_read() { before_ = now_; }

    // The menu opened: what is held is let go; closed: what the gamepads hold is quiet until let go.
    void menu_opened() noexcept { keyboard_ = 0; }
    void menu_closed() noexcept { hush_ = true; }
    // The menu asked for the original's pause box (where the level is given up) or its quit box: kept until a step
    // takes this machine's input.
    void give_up() noexcept { give_up_ = true; }
    void quit_box() noexcept { quit_box_ = true; }

    // The step's input: the keyboard word and the gamepads' (with the boxes asked for), the menus' keys among those
    // held now (`held`, by name), the stick's pace; Escape pressed since the last. Under Remaster rules the rewind is
    // held while its key or button is.
    Tick tick(const std::function<bool(std::string_view)>& held, Session& session);
    // After the step: whether it took this machine's input (the boxes asked for then forgotten), and what it left
    // (Alt+S and Alt+M once acted on; a scene loaded: nothing held until it is pressed again).
    void stepped(bool took_input, const Session& session);

    std::uint16_t keyboard() const noexcept { return keyboard_; }

  private:
    input::Actions key_actions(std::string_view key) const;
    bool act(input::Actions actions, Session& session) const;
    bool skips_demo(std::uint16_t keys) const;

    input::Controls controls_;
    input::PadReading now_{};
    input::PadReading before_{};
    input::PadReading quiet_{};   // held as the menu closed: not the game's until let go
    Play::MenuKeys sound_keys_;   // Alt+S and Alt+M, until a step acts on them
    std::uint16_t keyboard_ = 0;  // the keyboard word, as the original's handler keeps it
    bool remaster_;
    bool hush_ = false;  // the menu was closed: what the gamepads hold now is quiet
    bool give_up_ = false;
    bool quit_box_ = false;
    bool escaped_ = false;  // Escape pressed since the last step
};

}  // namespace lvr
