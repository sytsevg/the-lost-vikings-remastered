// The scenes before and between the worlds: the logos, the title with its menu, and the galaxy.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include "lvr/dialogue.hpp"
#include "lvr/enemies.hpp"
#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

// A logo: shown for a while (its argument, in ticks; 60 without one), or until Enter or F.
class Logo : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kLogoShows);
            world.animate(*this);
            count_down_from(argument != 0 ? argument : 60);
            return;
        }
        if (countdown == 0 || (world.pressed & key::kEnter) != 0 || (world.pressed & key::kF) != 0) {
            world.leave = true;
        }
    }
};

// The title. Left alone for 220 ticks it shows a demo. Enter or F opens the menu (the screen dims, clears and
// comes back with it): a new game, a password, or quit, chosen with up and down, or N, P and Q. The password's
// four letters are typed, or turned with up and down, left and right moving between them; a level's password
// starts that level.
class Title : public Entity {
  public:
    void update(World& world) override {
        const bool confirm = (world.pressed & key::kEnter) != 0 || (world.pressed & key::kF) != 0;
        switch (state_) {
            case State::kStart:
                world.play(*this, anim::kTitleStands);
                world.animate(*this);
                world.current_viking = 0;
                count_down_from(kPatience);
                state_ = State::kTitle;
                return;
            case State::kTitle:
                if (countdown == 0) {
                    return show_demo(world);
                }
                if (confirm) {
                    step_ = 0;
                    set_tint(world, kDimming.at(0));
                    state_ = State::kDimming;
                }
                return;
            case State::kDimming:
                if (++step_ < kDimming.size()) {
                    set_tint(world, kDimming.at(step_));
                    return;
                }
                world.title.screen_cleared = true;
                choice_ = 0;
                world.play(*this, anim::kTitlePointerAtChoice.at(0));
                step_ = 0;
                set_tint(world, kBrightening.at(0));
                state_ = State::kBrightening;
                return;
            case State::kBrightening:
                if (++step_ < kBrightening.size()) {
                    set_tint(world, kBrightening.at(step_));
                    return;
                }
                set_tint(world, 0);
                return show_menu(world);
            case State::kMenuComing:
                world.colour_boxes(0);
                put(world, 0xff, 14, 14);
                put(world, 0x100, 14, 16);
                put(world, 0x110, 14, 18);
                state_ = State::kMenuShown;
                return;
            case State::kMenuShown:
                world.show_pieces(*this);
                countdown = 200;
                world.play(*this, anim::kTitlePointer);
                state_ = State::kMenu;
                break;
            case State::kMenu:
                if ((world.pressed & key::kUp) != 0) {
                    choice_ = choice_ == 0 ? 2 : choice_ - 1;
                    chosen(world);
                } else if ((world.pressed & key::kDown) != 0) {
                    choice_ = choice_ == 2 ? 0 : choice_ + 1;
                    chosen(world);
                } else if (world.typed == 'N' ||
                           (world.typed != 'P' && world.typed != 'Q' && confirm && choice_ == 0)) {
                    world.sound(*this, effect::kTitleChosen);
                    state_ = State::kNewGame;
                    return;
                } else if (world.typed == 'P' || (world.typed != 'Q' && confirm && choice_ == 1)) {
                    world.sound(*this, effect::kTitlePassword);
                    world.play(*this, anim::kTitlePointerAtChoice.at(1));
                    world.animate(*this);
                    state_ = State::kPasswordComing;
                    return;
                } else if (world.typed == 'Q' || confirm) {
                    world.title.quit = true;
                    return;
                } else if (countdown == 0) {
                    return show_demo(world);
                }
                break;
            case State::kNewGame:
                world.title.demo_wanted = true;
                world.next_scene = scene::kIntro;
                world.leave = true;
                state_ = State::kDone;
                return;
            case State::kPasswordComing:
                world.play(*this, anim::kTitlePasswordStands);
                world.animate(*this);
                world.colour_boxes(0);
                put(world, 0x101, 14, 14);
                put(world, 0x100, 14, 16);
                put(world, 0x101, 14, 18);
                world.play(*this, anim::kTitlePointerAtLetter.at(0));
                at_ = 0;
                countdown = 0;
                world.typed = 0;
                state_ = State::kPassword;
                break;
            case State::kPassword:
                if (password(world, confirm)) {
                    return;
                }
                break;
            case State::kWrong:
                put(world, 0x10e, 10, 18);
                state_ = State::kWrongShown;
                return;
            case State::kWrongShown:
                countdown = 40;
                state_ = State::kWrongWaiting;
                return;
            case State::kWrongWaiting:
                if (!confirm && countdown != 0) {
                    return;
                }
                world.clear_text();
                choice_ = 1;
                world.play(*this, anim::kTitlePointerAtChoice.at(1));
                return show_menu(world);
            case State::kDone:
                return;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t {
        kStart,
        kTitle,
        kDimming,
        kBrightening,
        kMenuComing,
        kMenuShown,
        kMenu,
        kNewGame,
        kPasswordComing,
        kPassword,
        kWrong,
        kWrongShown,
        kWrongWaiting,
        kDone,
    };
    static constexpr std::array<std::uint8_t, 8> kDimming{4, 8, 12, 16, 20, 24, 28, 31};
    static constexpr std::array<std::uint8_t, 4> kBrightening{31, 24, 16, 8};
    static constexpr std::uint16_t kPatience = 220;
    static constexpr std::uint16_t kBackspace = 0x80;  // as typed characters
    static constexpr std::uint16_t kReturn = 0x81;

    static void set_tint(World& world, std::uint8_t by) {
        const auto doubled = static_cast<std::uint8_t>(by * 2U);
        world.tint = {doubled, doubled, doubled};
    }
    static void put(World& world, int text, int column, int row) {
        if (const auto line = text_at(world.texts, text, column, row)) {
            world.say(*line);
        }
    }

    void show_demo(World& world) {
        world.title.demo_wanted = true;
        world.leave = true;
        state_ = State::kDone;
    }
    void show_menu(World& world) {
        world.animate(*this);
        state_ = State::kMenuComing;
    }
    void chosen(World& world) {
        countdown = kPatience;
        world.sound(*this, effect::kMenuMoves);
        world.play(*this, anim::kTitlePointerAtChoice.at(static_cast<std::size_t>(choice_)));
    }

    // The letter after or before one, as up and down turn them: the digits, then the letters the passwords use.
    static std::uint8_t turned(std::uint8_t letter, bool up) {
        static constexpr std::array<std::array<std::uint8_t, 2>, 7> kSkips{
            {{0x5a, 0x30}, {0x39, 0x21}, {0x21, 0x42}, {0x44, 0x46}, {0x48, 0x4a}, {0x4e, 0x50}, {0x54, 0x56}}};
        for (const auto& [from, to] : kSkips) {
            if (letter == (up ? from : to)) {
                return up ? to : from;
            }
        }
        return static_cast<std::uint8_t>(up ? letter + 1 : letter - 1);
    }

    // One tick of the password being put in. True if the turn ended here.
    bool password(World& world, bool confirm) {
        std::uint8_t& letter = world.password.at(static_cast<std::size_t>(at_));
        if (world.typed != 0 && world.typed != kBackspace) {
            if (world.typed == kReturn) {
                return submit(world);
            }
            letter = static_cast<std::uint8_t>(world.typed);
            world.typed = 0;
            move(world, true);
            return false;
        }
        world.colour_boxes(0);
        for (std::size_t i = 0; i < world.password.size(); ++i) {
            world.say({.kind = Line::Kind::kCharacter,
                       .column = static_cast<std::uint16_t>(16 + i),
                       .row = 18,
                       .value = world.password.at(i)});
        }
        const bool up = (world.held & key::kUp) != 0;
        const bool down = !up && (world.held & key::kDown) != 0;
        const bool faster_down = !up && !down && (world.held & key::kPrevious) != 0;
        const bool faster_up = !up && !down && !faster_down && (world.held & key::kNext) != 0;
        if (up || down || faster_up || faster_down) {
            if (countdown == 0) {
                countdown = up || down ? 3 : 2;
                world.sound(*this, effect::kMenuMoves);
                letter = turned(letter, up || faster_up);
            }
            return false;
        }
        if (world.typed == kBackspace || (world.held & key::kLeft) != 0) {
            if (at_ != 0) {
                world.typed = 0;
                move(world, false);
            }
            return false;
        }
        if ((world.held & key::kRight) != 0) {
            move(world, true);
            return false;
        }
        if (world.typed != 'F' && confirm) {
            return submit(world);
        }
        return false;
    }

    void move(World& world, bool right) {
        if (right) {
            if (at_ == 3) {
                return;
            }
            ++at_;
            std::uint8_t& letter = world.password.at(static_cast<std::size_t>(at_));
            if (letter == ' ') {
                letter = 'B';
            }
        } else {
            --at_;
        }
        world.sound(*this, effect::kMenuMoves);
        world.play(*this, anim::kTitlePointerAtLetter.at(static_cast<std::size_t>(at_)));
    }

    // The cheat's password: the four letters the title's own script compares the typed ones with, read from the
    // scenes' database (each the low byte of a comparison's value). It turns the level keys on (World::Title).
    static std::array<std::uint8_t, 4> cheat_password(const World& world) {
        static constexpr std::array<std::size_t, 4> kLetterAt{0x898a, 0x8992, 0x899a, 0x89a2};
        std::array<std::uint8_t, 4> letters{};
        for (std::size_t i = 0; i < letters.size(); ++i) {
            if (kLetterAt.at(i) < world.programs.size()) {
                letters.at(i) = static_cast<std::uint8_t>(world.programs[kLetterAt.at(i)]);
            }
        }
        return letters;
    }

    bool submit(World& world) {
        if (world.password.at(0) != 0 && world.password == cheat_password(world)) {
            world.title.cheat = true;
            world.sound(*this, effect::kTitleChosen);
            world.clear_text();
            world.hide_pieces(*this);
            choice_ = 1;
            world.play(*this, anim::kTitlePointerAtChoice.at(1));
            show_menu(world);
            return true;
        }
        for (std::size_t level = 0; level < world.passwords.size(); ++level) {
            if (world.passwords.at(level) == world.password) {
                world.next_scene = static_cast<int>(level);
                world.sound(*this, effect::kTitleChosen);
                world.leave = true;
                state_ = State::kDone;
                return true;
            }
        }
        world.sound(*this, effect::kNothingToUse);
        world.hide_pieces(*this);
        state_ = State::kWrong;
        return true;
    }

    State state_ = State::kStart;
    std::size_t step_ = 0;
    int choice_ = 0;
    int at_ = 0;  // which of the password's letters the pointer is at
};

// The galaxy between two worlds: shown for its argument in ticks, then the first level of the world after the
// one the Vikings came from (after the last: the ending's village).
class Galaxy : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            // (The original also turns the music back on here if only the music was switched off; the music's
            // switch isn't the engine's.)
            world.next_scene = after(world.previous_scene);
            countdown = argument;
            return;
        }
        if (countdown == 0) {
            world.leave = true;
        }
    }

  private:
    static int after(int from) {
        // After a world's last level comes the next world's first; after the starfield, the ending.
        return level::ends_a_world(from + 1) || from == scene::kStarfield ? from + 1 : scene::kTitle;
    }
};

// One of the galaxy's lights: it flies a spiral, a speed a tick from a list (which of three places in it, by
// its argument), and stays where the list ends.
class GalaxyLight : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kGalaxyLightFirst);
            world.animate(*this);
            world.play(*this,
                       {static_cast<std::uint16_t>(anim::kGalaxyLightOwn.place + (4 * (argument <= 9 ? argument : 0))),
                        anim::kGalaxyLightOwn.home});
            world.animate(*this);
            world.play(*this, anim::kGalaxyLightFlies);
            max_step_x = px(16);
            max_step_y = px(16);
            at_ = argument >= 8 ? kLateStart : (argument < 3 ? kEarlyStart : 0);
        }
        const auto& speed = kSpeeds.at(std::min(at_, kSpeeds.size() - 1));
        ++at_;
        speed_x = speed[0];
        speed_y = speed[1];
        world.animate(*this);
    }

  private:
    static constexpr std::size_t kEarlyStart = 5;
    static constexpr std::size_t kLateStart = 19;
    static constexpr std::array<std::array<std::int16_t, 2>, 121> kSpeeds{
        {{-2407, 3313},  {-2996, 2769},  {-3470, 2111},  {-3805, 1363},  {-3982, 553},   {-3987, -283}, {-3813, -1110},
         {-3463, -1888}, {-2948, -2577}, {-2287, -3140}, {-1512, -3543}, {-659, -3762},  {226, -3777},  {1097, -3583},
         {1900, -3184},  {2587, -2601},  {3113, -1863},  {3439, -1014},  {3540, -106},   {3405, 799},   {3036, 1641},
         {2457, 2356},   {1705, 2891},   {835, 3201},    {-87, 3257},    {-988, 3051},   {-1793, 2597}, {-2432, 1929},
         {-2845, 1104},  {-2992, 195},   {-2857, -713},  {-2449, -1535}, {-1807, -2185}, {-995, -2597}, {-97, -2724},
         {787, -2552},   {1562, -2097},  {2134, -1412},  {2436, -577},   {2429, 303},    {2114, 1120},  {1532, 1765},
         {761, 2151},    {-89, 2225},    {-898, 1977},   {-1547, 1446},  {-1936, 713},   {-2007, -105}, {-1749, -875},
         {-1209, -1469}, {-482, -1787},  {300, -1774},   {996, -1437},   {1474, -843},   {1644, -110},  {1477, 611},
         {1012, 1174},   {353, 1460},    {-352, 1412},   {-944, 1046},   {-1286, 454},   {-1301, -217}, {-993, -801},
         {-446, -1149},  {188, -1176},   {738, -881},    {1050, -356},   {1042, 241},    {724, 734},    {203, 973},
         {-351, 890},    {-758, 523},    {-886, 2},      {-700, -486},   {-276, -771},   {222, -755},   {604, -454},
         {725, 3},       {547, 429},     {155, 649},     {-276, 577},    {-556, 256},    {-565, -156},  {-310, -467},
         {74, -532},     {394, -328},    {490, 26},      {321, 341},     {-7, 447},      {-307, 296},   {-407, -11},
         {-259, -288},   {33, -368},     {281, -211},    {328, 68},      {153, 279},     {-111, 282},   {-274, 86},
         {-226, -154},   {-13, -259},    {190, -157},    {226, 59},      {75, 208},      {-123, 170},   {-199, -10},
         {-91, -164},    {88, -155},     {168, -1},      {79, 138},      {-79, 128},     {-142, -10},   {-50, -124},
         {85, -93},      {113, 37},      {10, 112},      {-94, 48},      {-75, -66},     {34, -87},     {88, 3},
         {22, 80},       {0, 0}}};

    std::size_t at_ = 0;
};

}  // namespace

std::span<const KindOf> title_scenes() {
    static constexpr KindOf kList[] = {
        {Home::kScenes, scene_kind::kLogo, make<Logo>},
        {Home::kScenes, scene_kind::kTitle, make<Title>},
        {Home::kScenes, scene_kind::kGalaxy, make<Galaxy>},
        {Home::kScenes, scene_kind::kGalaxyLight, make<GalaxyLight>},
    };
    return kList;
}

}  // namespace lvr
