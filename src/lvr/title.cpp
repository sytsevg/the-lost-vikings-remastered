// The scenes before and between the worlds: the logos, the title with its menu, and the galaxy.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

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
        if (countdown == 0 || (world.host().pressed & key::kEnter) != 0 || (world.host().pressed & key::kF) != 0) {
            world.leave = true;
        }
    }
};

// The title. Left alone for 220 ticks it shows a demo. Enter or F opens the menu (the screen dims, clears and
// comes back with it): a new game, a password, or quit, chosen with up and down, or N, P and Q. Under Remaster
// rules offline also hosting a game online and joining one (ADR-0012), before quitting, or H and J: their pages are
// the Remaster menu's, over the title (the front end opens them, World::Title::online). The password's
// four letters are typed, or turned with up and down, left and right moving between them; a level's password
// starts that level.
class Title : public Entity {
  public:
    void update(World& world) override {
        const bool confirm = (world.host().pressed & key::kEnter) != 0 || (world.host().pressed & key::kF) != 0;
        world.title.page = 0;  // (asked for one tick: the front end has opened its page meanwhile)
        const int last = choices(world) - 1;
        switch (s.state) {
            case State::kStart:
                world.play(*this, anim::kTitleStands);
                animate_moved(world);
                world.host().viking = 0;
                count_down_from(kPatience);
                s.state = State::kTitle;
                return;
            case State::kTitle:
                if (countdown == 0) {
                    return show_demo(world);
                }
                if (confirm) {
                    s.step = 0;
                    set_tint(world, kDimming.at(0));
                    s.state = State::kDimming;
                }
                return;
            case State::kDimming:
                if (++s.step < kDimming.size()) {
                    set_tint(world, kDimming.at(s.step));
                    return;
                }
                world.title.screen_cleared = true;
                point_at(world, 0, row_of(world, 0));
                s.step = 0;
                set_tint(world, kBrightening.at(0));
                s.state = State::kBrightening;
                return;
            case State::kBrightening:
                if (++s.step < kBrightening.size()) {
                    set_tint(world, kBrightening.at(s.step));
                    return;
                }
                set_tint(world, 0);
                return show_menu(world);
            case State::kMenuComing:
                world.colour_boxes(0);
                put(world, 0xff, 14, row_of(world, 0));
                put(world, 0x100, 14, row_of(world, 1));
                if (last == kRemasterLast) {
                    write(world, "HOST GAME", 14, row_of(world, 2));
                    write(world, "JOIN GAME", 14, row_of(world, 3));
                    write(world, "OPTIONS", 14, row_of(world, 4));
                }
                put(world, 0x110, 14, row_of(world, last));
                if (last == kRemasterLast) {  // (from the password's screen: the menu's rows again)
                    point_at(world, s.choice, row_of(world, s.choice));
                }
                s.state = State::kMenuShown;
                return;
            case State::kMenuShown:
                world.show_pieces(*this);
                countdown = 200;
                world.play(*this, anim::kTitlePointer);
                s.state = State::kMenu;
                break;
            case State::kMenu:
                if ((world.host().pressed & key::kUp) != 0) {
                    chosen(world, s.choice == 0 ? last : s.choice - 1);
                } else if ((world.host().pressed & key::kDown) != 0) {
                    chosen(world, s.choice == last ? 0 : s.choice + 1);
                } else if (world.typed == 'N' ||
                           (world.typed != 'P' && world.typed != 'Q' && confirm && s.choice == 0)) {
                    world.sound(*this, effect::kTitleChosen);
                    s.state = State::kNewGame;
                    return;
                } else if (world.typed == 'P' || (world.typed != 'Q' && confirm && s.choice == 1)) {
                    world.sound(*this, effect::kTitlePassword);
                    // (The pointer stays on PASSWORD, wherever the menu has it, until the password's screen comes:
                    // the user, 2026-11-26, saw it jump to the original's row under Remaster rules' five.)
                    point_at(world, 1, row_of(world, 1));
                    animate_moved(world);
                    s.state = State::kPasswordComing;
                    return;
                } else if (const std::uint8_t page = page_chosen(world, confirm, last); page != 0) {
                    world.sound(*this, effect::kTitleChosen);
                    point_at(world, page + 1, row_of(world, page + 1));  // (HOST GAME's is the third choice)
                    world.title.page = page;
                    countdown = kPatience;
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
                s.state = State::kDone;
                return;
            case State::kPasswordComing:
                s.moved = 0;  // (the password's screen: the original's rows, PASSWORD's at kPasswordRow)
                world.play(*this, anim::kTitlePasswordStands);
                animate_moved(world);
                world.colour_boxes(0);
                for (int choice = 0; last == kRemasterLast && choice <= last; ++choice) {
                    put(world, 0x101, 14, row_of(world, choice));  // (the six choices' rows, cleared first)
                }
                put(world, 0x101, 14, 14);
                put(world, 0x100, 14, 16);
                put(world, 0x101, 14, 18);
                world.play(*this, anim::kTitlePointerAtLetter.at(0));
                s.at = 0;
                countdown = 0;
                world.typed = 0;
                s.state = State::kPassword;
                break;
            case State::kPassword:
                if (password(world, confirm)) {
                    return;
                }
                break;
            case State::kWrong:
                put(world, 0x10e, 10, 18);
                s.state = State::kWrongShown;
                return;
            case State::kWrongShown:
                countdown = 40;
                s.state = State::kWrongWaiting;
                return;
            case State::kWrongWaiting:
                if (!confirm && countdown != 0) {
                    return;
                }
                world.clear_text();
                s.choice = 1;  // (pointed at as the menu comes)
                return show_menu(world);
            case State::kDone:
                return;
        }
        animate_moved(world);
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
    static constexpr std::uint16_t kEscape = 0x82;  // (Remaster rules only: input::kTypedEscape)
    // The menu's choices: the original's three, two rows apart; under Remaster rules offline, HOST GAME, JOIN GAME
    // and OPTIONS between PASSWORD and QUIT TO DOS.
    static constexpr int kRowsApart = 2;
    static constexpr int kRemasterLast = 5;
    static constexpr int kPasswordRow = 16;
    // A choice's row: the original's from 14 on; with six, from 11, just under the logo (the text layer ends at row
    // 21).
    static int row_of(const World& world, int choice) {
        return (choices(world) == kRemasterLast + 1 ? 11 : 14) + (choice * kRowsApart);
    }
    static int choices(const World& world) {
        return world.rules == Rules::kRemaster && !world.online ? kRemasterLast + 1 : 3;
    }
    // HOST GAME, JOIN GAME (by their keys too) or OPTIONS chosen: the page the front end opens (World::Title::page);
    // else 0.
    std::uint8_t page_chosen(const World& world, bool confirm, int last) const {
        if (last != kRemasterLast || world.typed == 'Q' || world.typed == 'N' || world.typed == 'P') {
            return 0;
        }
        if (world.typed == 'H' || (confirm && s.choice == 2)) {
            return World::Title::kHostGame;
        }
        if (world.typed == 'J' || (confirm && s.choice == 3)) {
            return World::Title::kJoinGame;
        }
        return confirm && s.choice == 4 ? World::Title::kOptions : 0;
    }
    // A text of the Remaster's own in the font, a character a cell.
    static void write(World& world, std::string_view text, int column, int row) {
        for (std::size_t i = 0; i < text.size(); ++i) {
            world.say({.kind = Line::Kind::kCharacter,
                       .column = static_cast<std::uint16_t>(column + static_cast<int>(i)),
                       .row = static_cast<std::uint16_t>(row),
                       .value = static_cast<std::uint8_t>(text[i])});
        }
    }

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
        s.state = State::kDone;
    }
    void show_menu(World& world) {
        animate_moved(world);
        s.state = State::kMenuComing;
    }
    void chosen(World& world, int choice) {
        countdown = kPatience;
        world.sound(*this, effect::kMenuMoves);
        point_at(world, choice, row_of(world, choice));
    }
    // The pointer at a choice, on a row: the original's animation for the first three (rows 14, 16 and 18), the
    // third's for one further on; placed as many rows further as it takes (animate_moved).
    void point_at(World& world, int choice, int row) {
        constexpr int kCellPixels = 8;
        const int shown = std::min(choice, 2);
        s.moved = static_cast<std::int16_t>((row - (14 + (shown * kRowsApart))) * kCellPixels);
        s.choice = choice;
        world.play(*this, anim::kTitlePointerAtChoice.at(static_cast<std::size_t>(shown)));
    }
    // The animation run with the title that much further down (the pieces it places are placed from there), and
    // then back where it is: the camera follows the title, the menu's text doesn't.
    void animate_moved(World& world) {
        y = static_cast<std::int16_t>(y + s.moved);
        world.animate(*this);
        y = static_cast<std::int16_t>(y - s.moved);
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
        if (world.typed == kEscape) {  // back to the menu, as after a wrong password (the user, 2026-11-26)
            world.typed = 0;
            world.clear_text();
            s.choice = 1;
            show_menu(world);
            return true;
        }
        std::uint8_t& letter = world.password.at(static_cast<std::size_t>(s.at));
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
        const bool up = (world.host().held & key::kUp) != 0;
        const bool down = !up && (world.host().held & key::kDown) != 0;
        const bool faster_down = !up && !down && (world.host().held & key::kPrevious) != 0;
        const bool faster_up = !up && !down && !faster_down && (world.host().held & key::kNext) != 0;
        if (up || down || faster_up || faster_down) {
            if (countdown == 0) {
                countdown = up || down ? 3 : 2;
                world.sound(*this, effect::kMenuMoves);
                letter = turned(letter, up || faster_up);
            }
            return false;
        }
        if (world.typed == kBackspace || (world.host().held & key::kLeft) != 0) {
            if (s.at != 0) {
                world.typed = 0;
                move(world, false);
            }
            return false;
        }
        if ((world.host().held & key::kRight) != 0) {
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
            if (s.at == 3) {
                return;
            }
            ++s.at;
            std::uint8_t& letter = world.password.at(static_cast<std::size_t>(s.at));
            if (letter == ' ') {
                letter = 'B';
            }
        } else {
            --s.at;
        }
        world.sound(*this, effect::kMenuMoves);
        world.play(*this, anim::kTitlePointerAtLetter.at(static_cast<std::size_t>(s.at)));
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
            point_at(world, 1, kPasswordRow);
            show_menu(world);
            return true;
        }
        for (std::size_t level = 0; level < world.passwords.size(); ++level) {
            if (world.passwords.at(level) == world.password) {
                world.next_scene = static_cast<int>(level);
                world.sound(*this, effect::kTitleChosen);
                world.leave = true;
                s.state = State::kDone;
                return true;
            }
        }
        world.sound(*this, effect::kNothingToUse);
        world.hide_pieces(*this);
        s.state = State::kWrong;
        return true;
    }

    struct Saved {
        State state = State::kStart;
        std::size_t step = 0;
        int choice = 0;
        int at = 0;              // which of the password's letters the pointer is at
        std::int16_t moved = 0;  // how much further down the pointer is placed, in pixels: the choices further on
    } s;
    LVR_SAVES(Entity)
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
            s.at = argument >= 8 ? kLateStart : (argument < 3 ? kEarlyStart : 0);
        }
        const auto& speed = kSpeeds.at(std::min(s.at, kSpeeds.size() - 1));
        ++s.at;
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

    struct Saved {
        std::size_t at = 0;
    } s;
    LVR_SAVES(Entity)
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
