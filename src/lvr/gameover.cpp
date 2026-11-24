// The game-over scene's kinds: the director with its question and its lightning, the three Vikings' figures, the
// burning ship and the letters that drop in. Every world's object database has them; read from the spaceship's.

#include <array>
#include <cstdint>
#include <memory>

#include "lvr/dialogue.hpp"
#include "lvr/kinds.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

bool any(std::uint16_t keys, std::uint16_t a, std::uint16_t b, std::uint16_t c = 0) {
    return (keys & a) != 0 || (keys & b) != 0 || (c != 0 && (keys & c) != 0);
}

// The scene's director. It waits for a key or 200 ticks, then asks TRY AGAIN, yes or no (left and right choose,
// Y and N answer at once). Yes, or a key before the question: its lightning gives each dead Viking his health
// back, and the level starts again. No: GAME OVER drops in, and the game goes back to the title. E, S or D show
// the level's password meanwhile.
class GameOver : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                return at_start(world);
            case State::kWaiting:
                return when_waiting(world);
            case State::kAsking:
                return when_asking(world);
            case State::kAnswered:
                return when_answered(world);
            case State::kPasswordComing:
                return when_password_coming(world);
            case State::kPasswordShown:
                if (any(world.pressed, key::kF, key::kS) || any(world.pressed, key::kE, key::kD)) {
                    world.clear_text();
                    state_ = State::kPasswordGoing;
                }
                return;
            case State::kPasswordGoing:
                return when_password_going(world);
            case State::kStriking:
                return when_striking(world);
            case State::kStruck:
                return when_struck(world);
            case State::kBetweenStrikes:
                if (countdown == 0) {
                    revive(world);
                }
                return;
            case State::kLeaving:
                return when_leaving(world);
            case State::kDone:
                return;
        }
    }
    void at_start(World& world) {
        world.play(*this, anim::kGameOverStands);
        world.animate(*this);
        world.sound(*this, effect::kDirge);
        world.game_over_scene.letters_fall = false;
        count_down();
        world.current_viking = 0;
        countdown = 200;
        state_ = State::kWaiting;
    }
    void when_waiting(World& world) {
        if (any(world.pressed, key::kEnter, key::kF)) {
            return revive(world);
        }
        if (any(world.pressed, key::kS, key::kE, key::kD)) {
            return show_password(world, false);
        }
        if (countdown != 0) {
            return;
        }
        countdown = kBlinkTicks;
        yes_ = true;
        lit_ = true;
        ask(world);
        state_ = State::kAsking;
        return show_choice(world);
    }
    void when_asking(World& world) {
        if ((world.held & key::kLeft) != 0) {
            yes_ = true;
        } else if ((world.held & key::kRight) != 0) {
            yes_ = false;
        } else if (any(world.held, key::kEnter, key::kF) || world.typed == 'Y' || world.typed == 'N') {
            world.clear_text();
            state_ = State::kAnswered;
            return;
        } else if (any(world.pressed, key::kS, key::kE, key::kD)) {
            return show_password(world, true);
        } else if (countdown == 0) {
            countdown = kBlinkTicks;
            lit_ = !lit_;
        }
        return show_choice(world);
    }
    void when_answered(World& world) {
        if (world.typed == 'Y' || (world.typed != 'N' && yes_)) {
            return revive(world);
        }
        world.game_over_scene.letters_fall = true;
        countdown = 200;
        state_ = State::kLeaving;
    }
    void when_password_coming(World& world) {
        world.colour_boxes(0);
        world.say_at_cell(kPasswordText, tail::kNone, 15, 10);
        for (std::size_t i = 0; i < world.password.size(); ++i) {
            world.say({.kind = Line::Kind::kCharacter,
                       .column = static_cast<std::uint16_t>(18 + i),
                       .row = 12,
                       .value = world.password.at(i)});
        }
        state_ = State::kPasswordShown;
    }
    void when_password_going(World& world) {
        if (!asked_) {
            state_ = State::kWaiting;
            return;
        }
        ask(world);
        state_ = State::kAsking;
        return show_choice(world);
    }
    void when_striking(World& world) {
        if (animation_over()) {
            world.game_over_scene.kept_health.at(static_cast<std::size_t>(struck_)) = kFullHealth;
            world.play(*this, anim::kGameOverStruck.at(static_cast<std::size_t>(struck_)));
            state_ = State::kStruck;
        }
        world.animate(*this);
    }
    void when_struck(World& world) {
        if (animation_over()) {
            countdown = 30;
            state_ = State::kBetweenStrikes;
            return;
        }
        world.animate(*this);
    }
    void when_leaving(World& world) {
        if (countdown == 0 || any(world.pressed, key::kEnter, key::kF)) {
            world.stop_sound(*this, effect::kDirge);
            world.game_over_scene.give_up = true;
            state_ = State::kDone;
        }
    }

  private:
    enum class State : std::uint8_t {
        kStart,
        kWaiting,
        kAsking,
        kAnswered,
        kPasswordComing,
        kPasswordShown,
        kPasswordGoing,
        kStriking,
        kStruck,
        kBetweenStrikes,
        kLeaving,
        kDone,
    };
    // The lightning on Erik, Baleog and Olaf, and what follows it.
    static constexpr std::uint16_t kBlinkTicks = 7;
    static constexpr int kQuestionText = 0x10f;

    void ask(World& world) const {
        world.colour_boxes(0);
        world.say_at_cell(kQuestionText, tail::kNone, 13, 8);
    }

    // YES and NO; the chosen one blinks.
    void show_choice(World& world) const {
        const auto put = [&world](int text, int column) {
            if (const auto line = text_at(world.texts, text, column, 11)) {
                world.say(*line);
            }
        };
        if (yes_) {
            put(lit_ ? 5 : 6, 15);
            put(4, 21);
        } else {
            put(5, 15);
            put(lit_ ? 4 : 6, 21);
        }
    }

    void show_password(World& world, bool asked) {
        asked_ = asked;
        world.clear_text();
        state_ = State::kPasswordComing;
    }

    // The next dead Viking is struck (Baleog first, then Erik, then Olaf); with none left the level starts again.
    void revive(World& world) {
        for (const int who : {1, 0, 2}) {
            if (world.game_over_scene.kept_health.at(static_cast<std::size_t>(who)) == 0) {
                struck_ = who;
                world.play(*this, anim::kGameOverStrikes.at(static_cast<std::size_t>(who)));
                state_ = State::kStriking;
                world.animate(*this);
                return;
            }
        }
        // (The original gives the items back here, from words nothing writes (35BF in the listing); the level's
        // start clears them anyway.)
        world.stop_sound(*this, effect::kDirge);
        world.game_over_scene.try_again = true;
        state_ = State::kDone;
    }

    State state_ = State::kStart;
    bool yes_ = true;
    bool lit_ = true;
    bool asked_ = false;
    int struck_ = 0;
};

// One Viking's figure: shown while he has health, unseen while he is dead.
class Figure : public Entity {
  public:
    void update(World& world) override {
        const std::size_t who =
            kind == game_over_kind::kBaleogsFigure ? 1 : (kind == game_over_kind::kEriksFigure ? 0 : 2);
        if (shown_) {
            return;
        }
        if (world.game_over_scene.kept_health.at(who) == 0) {
            if (!hidden_) {
                hidden_ = true;
                world.hide_pieces(*this);
            }
            return;
        }
        world.show_pieces(*this);
        world.play(*this, anim::kGameOverVikingStands.at(who));
        world.animate(*this);
        shown_ = true;
    }

  private:
    bool shown_ = false;
    bool hidden_ = false;
};

// A part of the burning ship that drifts away: there only when a Viking is dead.
class BurningShip : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            if (world.game_over_scene.kept_health.at(0) != 0 && world.game_over_scene.kept_health.at(1) != 0 &&
                world.game_over_scene.kept_health.at(2) != 0) {
                world.remove(slot);
                return;
            }
            world.play(*this, anim::kBurningShipAppears);
            world.animate(*this);
            world.play(*this, anim::kBurningShipParts.at(argument < anim::kBurningShipParts.size() ? argument : 0));
            world.animate(*this);
            speed_x = px(0.5);
            speed_y = px(-0.125);
            world.play(*this, anim::kBurningShipBurns);
        }
        world.animate(*this);
    }
};

// One of the four parts of GAME OVER: when the players give up it drops in, bounces once and lies there.
class Letters : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart: {
                world.play(*this, anim::kLettersAppears);
                world.animate(*this);
                if (argument >= anim::kGameOverLetters.size()) {
                    world.remove(slot);
                    return;
                }
                world.play(*this, anim::kGameOverLetters.at(argument));
                state_ = State::kWaiting;
                break;
            }
            case State::kWaiting:
                if (world.game_over_scene.letters_fall) {
                    start_falling();
                    state_ = State::kFalling;
                }
                break;
            case State::kFalling:
                if (y >= 80) {
                    speed_y = px(-6);
                    lands_at_ = y;
                    thud(world);
                    state_ = State::kBouncing;
                }
                break;
            case State::kBouncing:
                if (speed_y >= 0 && static_cast<std::uint16_t>(y) >= static_cast<std::uint16_t>(lands_at_)) {
                    stop_falling();
                    thud(world);
                    state_ = State::kLying;
                    return;
                }
                break;
            case State::kLying:
                return;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kFalling, kBouncing, kLying };

    void thud(World& world) const { world.sound(*this, effect::kThud3C); }

    State state_ = State::kStart;
    std::int16_t lands_at_ = 0;
};

}  // namespace

std::unique_ptr<Entity> make_game_over(std::uint16_t kind) {
    switch (kind) {
        case game_over_kind::kGameOver:
            return make_entity<GameOver>();
        case game_over_kind::kBaleogsFigure:
        case game_over_kind::kEriksFigure:
        case game_over_kind::kOlafsFigure:
            return make_entity<Figure>();
        case game_over_kind::kBurningShip:
            return make_entity<BurningShip>();
        case game_over_kind::kLetters:
            return make_entity<Letters>();
        default:
            return nullptr;
    }
}

}  // namespace lvr
