// The kinds that talk: the level's start (its password, the Vikings' opening words).

#include <array>
#include <span>
#include <vector>

#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/shared.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

// The inside of each Viking's boxes.
// Anchors: where a box's tail is.
constexpr int kTailBottomLeft = 1;

// The rows of a level: its opening's, or its ending's.
template <std::size_t Count>
std::vector<talk::Said> rows_of(const std::array<talk::LevelSaid, Count>& all, int level) {
    std::vector<talk::Said> rows;
    for (const talk::LevelSaid& one : all) {
        if (one.level == level) {
            rows.push_back(one.said);
        }
    }
    return rows;
}

}  // namespace

void speak(World& world, const talk::Said& said, const Entity* host, int viking) {
    using talk::Who;
    const bool needs_host =
        said.who == Who::kHost || said.who == Who::kVikingBeside || said.who == Who::kVikingBesideHisOwn;
    if (needs_host && host == nullptr) {
        return;  // a row of a talk with someone, said by a kind that isn't one
    }
    if (said.who == Who::kVoice) {
        if (said.colour) {
            world.colour_boxes(text_colour::kOthers);
        }
        world.say_at_cell(said.text, said.anchor, 8, 1);
    } else if (said.who == Who::kHost) {
        world.colour_boxes(text_colour::kOthers);
        world.say_at_point(said.text, said.anchor, host->x, host->y + said.below);
    } else {
        const int who = talk::slot_of(said.who) != kNobody ? talk::slot_of(said.who) : viking;
        const Entity* speaker = world.at(who);
        if (speaker == nullptr) {
            return;
        }
        const bool beside = said.who == Who::kVikingBeside || said.who == Who::kVikingBesideHisOwn;
        const int his_own = who == slot::kBaleog ? 0 : (who == slot::kErik ? 1 : 2);
        if (said.colour) {
            world.colour_boxes(text_colour::of(who));
        }
        world.say_at_point(said.text + (said.who == Who::kVikingBesideHisOwn ? his_own : 0), said.anchor, speaker->x,
                           (beside ? host->y : speaker->y) + said.below);
    }
    world.wait_for_key();
    world.clear_text();
}

void speak(World& world, std::span<const talk::Said> rows, const Entity* host, int viking) {
    for (const talk::Said& said : rows) {
        speak(world, said, host, viking);
    }
}

namespace {

// The level's start: invisible, always active, gone once it has had its say. It puts the game's state for a new
// level in order, shows the level's password, waits for the Vikings to arrive where a level has them come in, and
// then has them say their opening lines.
class LevelStart : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kBegin:
                begin(world);
                return;
            case State::kArriving:
                if (world.vikings_arriving == 0) {
                    opening(world);
                }
                return;
            case State::kOpening:
                opening(world);
                return;
            case State::kFirstLevel:
                first_level(world);
                return;
            case State::kBanter:
                banter(world);
                return;
            case State::kSecondPart:
            case State::kThirdPart:
                later_part(world);
                return;
        }
    }
    void outside_active_area(World& world) override { world.remove(slot); }

  private:
    enum class State : std::uint8_t { kBegin, kArriving, kOpening, kFirstLevel, kBanter, kSecondPart, kThirdPart };

    // It has had its say, for good.
    void done(World& world) { gone_for_good(world, *this); }

    void begin(World& world) {
        for (int viking = 0; viking < kVikingSlots; ++viking) {
            if (Entity* entity = world.at(viking)) {
                entity->health = kFullHealth;
            }
        }
        // In the first level of every world after the first, the Vikings fall in from above.
        const bool comes_in = level::begins_a_later_world(world.level) || world.scene == scene::kEnding;
        world.vikings_falling_in = comes_in ? 7 : 0;
        if (world.level == level::kCountsWithValues) {
            world.values = {3, 2, 0, 1, 0, 0, 0, 0};
        }
        if (world.after_game_over) {
            ++world.retries;
            if (world.level == level::kFirst || world.level == level::kLast) {
                done(world);
                return;
            }
        } else {
            world.retries = 0;
            if (world.attract || world.level > level::kLast || world.level == 0) {  // a demo, or not a level
                done(world);
                return;
            }
            if (world.level > level::kFirst) {  // the first has no password
                world.colour_boxes(0);
                world.say_at_cell(kPasswordText, tail::kNone, 15, 10);
                for (std::size_t i = 0; i < world.password.size(); ++i) {
                    world.say({.kind = Line::Kind::kCharacter,
                               .column = static_cast<std::uint16_t>(18 + i),
                               .row = 12,
                               .value = world.password.at(i)});
                }
                world.wait_for_key();
                world.clear_text();
            }
        }
        // Olaf's boxes have their tail on the side he faces away from.
        const Entity* olaf = world.entities[slot::kOlaf].get();
        s.olaf_anchor = olaf != nullptr && olaf->facing_left() ? 2 : kTailBottomLeft;
        if (comes_in) {
            world.vikings_arriving = 7;
            s.state = State::kArriving;
        } else {
            s.state = State::kOpening;
        }
    }

    // Which conversation, by how often the level was lost, then by the level.
    void opening(World& world) {
        if (world.retries != 0) {
            // After the fifth, eighth, eleventh, fifteenth, fiftieth and fifty-first loss in a row the Vikings
            // have something to say about it.
            if (world.retries != 5 && world.retries != 8 && world.retries != 11 && world.retries != 15 &&
                world.retries != 50 && world.retries != 51) {
                done(world);
                return;
            }
            for (int viking = 0; viking < kVikingSlots; ++viking) {
                world.tell(viking, message::kConversation);
            }
            s.state = State::kBanter;
            return;
        }
        // The first level of each world has an opening, and the level the Vikings find their way home in.
        if (world.level != level::kFirst && !level::begins_a_later_world(world.level) &&
            world.level != level::kWayHome) {
            done(world);
            return;
        }
        // The Vikings are told a conversation starts; it does in the next tick.
        for (int viking = 0; viking < kVikingSlots; ++viking) {
            world.tell(viking, message::kConversation);
        }
        s.state = State::kFirstLevel;
    }

    // What they say after losing again: one of a few conversations, picked at random (who speaks: 0 Erik, 1
    // Baleog, 2 Olaf, 3 the game itself in a box at the top; and the text).
    void banter(World& world) {
        world.sound(*this, effect::kTalk);
        std::size_t talk = 0;
        if (world.retries == 5) {  // one of eight
            const std::uint16_t dice = world.random();
            talk = dice >= 57344 ? 0 : std::size_t{1} + (dice >> 13U);
        } else if (world.retries == 8) {  // one of three
            const std::uint16_t dice = world.random();
            talk = dice < 21845 ? 9 : (dice < 43690 ? 10 : 8);
        } else if (world.retries == 11) {  // one of two
            talk = world.random() < 32768 ? 12 : 11;
        } else {
            talk = world.retries == 15 ? 13 : (world.retries == 50 ? 14 : 15);
        }
        for (talk::Said said : talk::kRetries.at(talk)) {
            if (said.text == 0) {
                break;
            }
            if (said.who == talk::Who::kBaleog || said.who == talk::Who::kOlaf) {
                said.anchor = s.olaf_anchor;
            }
            speak(world, said);
        }
        done(world);
    }

    void first_level(World& world) {
        world.sound(*this, effect::kTalk);
        if (const auto opening = rows_of(talk::kOpenings, world.level); !opening.empty()) {
            speak(world, opening);
            done(world);
        } else {  // the last level: the Vikings find the way home, in three parts
            speak(world, talk::kWayHomeFirst);
            s.state = State::kSecondPart;
        }
    }

    void later_part(World& world) {
        if (s.state == State::kSecondPart) {
            speak(world, talk::kWayHomeSecond);
            s.state = State::kThirdPart;
        } else {
            speak(world, talk::kWayHomeThird);
            done(world);
        }
    }

    struct Saved {
        State state = State::kBegin;
        int olaf_anchor = kTailBottomLeft;
    } s;
    LVR_SAVES(Entity)
};

// A hint box: a Viking who asks it (the action key beside it) is shown its hint; the kind that greets also shows
// it the first time the Viking being played touches it.
class Hint : public Entity {
  public:
    explicit Hint(bool greets) { s.greets = greets; }

    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                acts_as = tile::kThing;
                world.play(*this, anim::kHintStands);
                world.animate(*this);
                s.state = State::kAnswering;
                if (s.greets) {
                    // Its argument is the hint's number and, above that, its bit among the hints seen.
                    s.seen_bit = argument & kHighByte;
                    argument &= kLowByte;
                    if ((world.hints_seen & s.seen_bit) != s.seen_bit) {
                        s.state = State::kGreeting;
                    }
                }
                return;
            case State::kGreeting:
                if (saw.viking != kNobody) {
                    target = saw.viking;
                    const Entity* viking = world.at(saw.viking);
                    if (viking != nullptr && world.can_be_spoken_to(*viking)) {
                        show(world);
                    }
                    return;
                }
                answer(world);
                return;
            case State::kAnswering:
                answer(world);
                return;
        }
    }
    Looks looks_for() const override { return s.state == State::kGreeting ? Looks{touching_a_viking()} : Looks{}; }

  private:
    enum class State : std::uint8_t { kStart, kGreeting, kAnswering };
    static constexpr int kFirstHintText = 0x40;
    static constexpr int kAboveIt = 24;

    // A Viking asked: the hint; anything else it is told is forgotten.
    void answer(World& world) {
        if (message == 0) {
            return;
        }
        if (was_asked(*this)) {
            show(world);
        }
    }

    void show(World& world) {
        world.sound(*this, effect::kTalk);
        world.speak_at_point(text_colour::kErik, kFirstHintText + argument, tail::kNone, x, y - kAboveIt);
        world.hints_seen |= s.seen_bit;
        message = 0;
        s.state = State::kAnswering;
    }

    struct Saved {
        bool greets;
        State state = State::kStart;
        std::uint16_t seen_bit = 0;
    } s;
    LVR_SAVES(Entity)
};

// A stop on the ship's last level (kind 56), unseen. When the three Vikings are all within 32 pixels of it they
// have a word with the voice from above and are beamed on: the first stop's further on, the second's (argument 1)
// up to its ruler, where they speak again once all three have appeared. One of them gone: the game-over scene.
class ShipStop : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                s.state = State::kWaiting;
                return;
            case State::kWaiting:
                return when_waiting(world);
            case State::kTalking:
                return when_talking(world);
            case State::kBeaming:
                beam(world, 72, 960);
                world.remove(slot);
                return;
            case State::kWaitingForThem:
                return when_waiting_for_them(world);
            case State::kTalkingAgain:
                say_all(world, talk::kShipSecondStopAfter);
                world.remove(slot);
                return;
        }
    }
    void when_waiting(World& world) {
        if (!all_vikings_here(world, *this, s.missing)) {
            return;
        }
        if (s.missing) {
            world.next_scene = scene::kGameOver;
            world.leave = true;
            return;
        }
        stay_active();
        tell_all(world, message::kConversation);
        s.state = State::kTalking;
    }
    void when_talking(World& world) {
        world.sound(*this, effect::kTalk);
        if (argument == 1) {
            say_all(world, talk::kShipSecondStop);
            beam(world, 112, 432);
            s.state = State::kWaitingForThem;
            return;
        }
        say_all(world, talk::kShipFirstStop);
        s.state = State::kBeaming;
    }
    void when_waiting_for_them(World& world) {
        for (const int viking : {slot::kErik, slot::kBaleog, slot::kOlaf}) {
            target = viking;
            const Entity* he = world.at(viking);
            if (he == nullptr || he->hit_class != hit_class::kViking) {
                return;
            }
        }
        tell_all(world, message::kConversation);
        s.state = State::kTalkingAgain;
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kTalking, kBeaming, kWaitingForThem, kTalkingAgain };

    void tell_all(World& world, std::uint16_t what) {
        for (const int viking : {slot::kBaleog, slot::kErik, slot::kOlaf}) {  // in the original's order
            target = viking;
            world.tell(viking, what);
        }
    }
    // Each named Viking is its target while he speaks.
    void say_all(World& world, std::span<const talk::Said> rows) {
        for (const talk::Said& said : rows) {
            if (talk::slot_of(said.who) != kNobody) {
                target = talk::slot_of(said.who);
            }
            speak(world, said);
        }
    }
    void beam(World& world, int to_x, int to_y) {
        world.beam = {.x = to_x, .y = to_y, .faces_right = true};
        tell_all(world, message::kBeamed);
    }

    struct Saved {
        State state = State::kStart;
        bool missing = false;
    } s;
    LVR_SAVES(Entity)
};

// Where most levels end: an unseen place. When the three Vikings are all within 32 pixels of it they have a
// word about the level (each level its own), and the level is done.
class LevelEnd : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                s.state = State::kWaiting;
                return;
            case State::kWaiting:
                return when_waiting(world);
            case State::kTalking:
                return when_talking(world);
            case State::kSaid:
                world.level_done = true;
                return;
        }
    }
    void when_waiting(World& world) {
        if (!all_vikings_here(world, *this, s.missing)) {
            return;
        }
        if (s.missing) {  // not all three: no words, and the scene after this is the game-over one
            world.next_scene = scene::kGameOver;
            world.leave = true;
            return;
        }
        if (rows_of(talk::kEndings, world.scene).empty()) {
            world.level_done = true;
            return;
        }
        for (int viking = 0; viking < kVikingSlots; ++viking) {
            world.tell(viking, message::kConversation);
        }
        target = slot::kOlaf;
        s.state = State::kTalking;
    }
    void when_talking(World& world) {
        world.sound(*this, effect::kTalk);
        for (const talk::Said& said : rows_of(talk::kEndings, world.scene)) {
            if (talk::slot_of(said.who) != kNobody) {
                target = talk::slot_of(said.who);
            }
            speak(world, said);
        }
        s.state = State::kSaid;
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kTalking, kSaid };
    struct Saved {
        State state = State::kStart;
        bool missing = false;
    } s;
    LVR_SAVES(Entity)
};

// Whether a Viking can be spoken to by an entity: on his feet, the one being played with the view at rest, and
// the entity itself well in view.
bool can_talk(const World& world, const Entity& speaker, int viking_slot) {
    const Entity* viking = viking_slot >= 0 && viking_slot < world.slots() ? world.at(viking_slot) : nullptr;
    return viking != nullptr && !viking->is_falling() && world.can_be_spoken_to(*viking) && world.well_in_view(speaker);
}

// Someone to talk to (the spaceship's second level): once his switches are on, the Viking being played who comes
// up to him has a long word with him; after that a Viking who asks gets a short one.
class Janitor : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kThing;
            world.play(*this, anim::kJanitorStands);
            world.animate(*this);
            return;
        }
        if (switches_on(world, argument)) {
            if ((world.hints_seen & argument) == argument) {
                if (was_asked(*this)) {
                    talk(world, true);
                }
            } else if (message == message::kAsked) {
                message = 0;
                target = message_value;
                talk(world, false);
            } else {
                message = 0;
                if (viking_comes_near(world, *this, 30, -10)) {
                    talk(world, false);
                }
            }
        }
        world.animate(*this);
    }

  private:
    void talk(World& world, bool again) {
        if (!can_talk(world, *this, target)) {
            return;
        }
        world.sound(*this, effect::kTalk);
        if (again) {
            speak(world, talk::kJanitorAgain, this, target);
        } else {
            speak(world, talk::kJanitor, this, target);
            world.hints_seen |= argument;
        }
        message = 0;
    }
};

// Another one to talk to: while his switches are off he says one thing to a Viking who comes near, once; when
// they are on, another, and that again to whoever asks.
class Mechanic : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kThing;
            world.play(*this, anim::kMechanicStands);
            world.animate(*this);
            return;
        }
        if ((world.hints_seen & argument) == argument || s.said) {
            if (was_asked(*this)) {
                say(world, 0x30, tail::kBelowMiddle, -24, (world.hints_seen & argument) != argument);
            }
        } else if (switches_on(world, argument)) {
            if (viking_comes_near(world, *this, 30, -10)) {
                say(world, 0x30, tail::kBelowMiddle, -24, true);
            }
        } else if (!s.warned && viking_comes_near(world, *this, 60, -5)) {
            if (can_talk(world, *this, target)) {
                world.sound(*this, effect::kTalk);
                world.speak_at_point(text_colour::kOthers, 0x2f, tail::kAboveLeft, x, y + 24);
                s.warned = true;
                message = 0;
            }
        }
        world.animate(*this);
    }

  private:
    void say(World& world, int text, int anchor, int above, bool first) {
        if (!can_talk(world, *this, target)) {
            return;
        }
        world.sound(*this, effect::kTalk);
        world.speak_at_point(text_colour::kOthers, text, anchor, x, y + above);
        if (first) {
            world.hints_seen |= argument;
        }
        s.said = true;
        message = 0;
    }

    struct Saved {
        bool said = false;
        bool warned = false;
    } s;
    LVR_SAVES(Entity)
};

// The prehistoric world's elder: once his switches are on, the Viking who comes up to him hears him out, and that
// changes other switches (his argument's low byte); after that, whoever asks gets the short of it.
class Elder : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kThing;
            s.changes = argument & kLowByte;
            argument &= kHighByte;
            world.play(*this, anim::kElderStands);
            world.animate(*this);
            return;
        }
        const bool known = (world.hints_seen & argument) == argument;
        if (known || s.said) {
            if (was_asked(*this)) {
                talk(world, !known);
            }
        } else if (switches_on(world, argument)) {
            if (viking_comes_near(world, *this, 30, -30)) {
                talk(world, true);
            }
        }
        world.animate(*this);
    }

  private:
    void talk(World& world, bool whole) {
        if (!can_talk(world, *this, target)) {
            return;
        }
        world.sound(*this, effect::kTalk);
        if (whole) {
            speak(world, talk::kElder, this, target);
            world.hints_seen |= argument;
            world.switches ^= s.changes;
        } else {
            speak(world, talk::kElderAgain, this, target);
        }
        s.said = true;
        message = 0;
    }

    struct Saved {
        bool said = false;
        std::uint16_t changes = 0;
    } s;
    LVR_SAVES(Entity)
};

// A place the Viking being played remarks on when he comes to it (two boxes, by its argument), once.
class Remark : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                s.state = State::kWaiting;
                return;
            case State::kWaiting: {
                if (saw.other == kNobody) {
                    return;
                }
                target = saw.other;
                Entity* viking = world.at(saw.other);
                if (viking == nullptr || !world.can_be_spoken_to(*viking)) {
                    return;
                }
                viking->message = message::kConversation;
                s.state = State::kSaying;
                return;
            }
            case State::kSaying: {
                world.sound(*this, effect::kTalk);
                if (const Entity* viking = world.at(target)) {
                    const int text = 0x4f + argument;
                    world.speak_at_point(text_colour::of(target), text, tail::kAboveMiddle, viking->x, viking->y + 20);
                    world.speak_at_point(text_colour::of(target), text + 1, tail::kAboveMiddle, viking->x,
                                         viking->y + 20);
                }
                gone_for_good(world, *this);
                return;
            }
        }
    }
    Looks looks_for() const override {
        return s.state == State::kWaiting ? Looks{touching_any(hit::kVikingInField, &Saw::other)} : Looks{};
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kSaying };

    struct Saved {
        State state = State::kStart;
    } s;
    LVR_SAVES(Entity)
};

// Another of the spaceship's to talk to: the Viking who comes up to him gets the whole story, and whoever asks
// after that gets it again.
class Prisoner : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kThing;
            world.play(*this, anim::kPrisonerStands);
            world.animate(*this);
            return;
        }
        if ((world.hints_seen & argument) == argument) {
            if (was_asked(*this)) {
                talk(world);
            }
        } else {
            if (viking_comes_near(world, *this, 30, -5)) {
                talk(world);
            }
        }
        world.animate(*this);
    }

  private:
    void talk(World& world) {
        if (!can_talk(world, *this, target)) {
            return;
        }
        world.sound(*this, effect::kTalk);
        speak(world, talk::kPrisoner, this, target);
        world.hints_seen |= argument;
        message = 0;
    }
};

// The prehistoric world's man to talk to: the Viking who comes up to him gets the long story, once; after that,
// or once his hint is known, a Viking who asks gets a short one.
class Caveman : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kThing;
            world.play(*this, anim::kCavemanStands);
            world.animate(*this);
            return;
        }
        const bool known = (world.hints_seen & argument) == argument;
        if (known || s.said) {
            if (was_asked(*this)) {
                talk(world, !known);
            }
        } else {
            if (viking_comes_near(world, *this, 30, -5)) {
                talk(world, true);
            }
        }
        world.animate(*this);
    }

  private:
    void talk(World& world, bool long_story) {
        if (!can_talk(world, *this, target)) {
            return;
        }
        world.sound(*this, effect::kTalk);
        if (long_story) {
            speak(world, talk::kCaveman, this, target);
            world.hints_seen |= argument;
        } else {
            speak(world, talk::kCavemanAgain, this, target);
        }
        s.said = true;
        message = 0;
    }

    struct Saved {
        bool said = false;
    } s;
    LVR_SAVES(Entity)
};

// The exit: when the three Vikings are all at it, the level is done. In the last level of each world they first
// have a word about it.
class Exit : public Entity {
  public:
    void update(World& world) override {
        switch (s.state) {
            case State::kStart:
                world.play(*this, anim::kExitStands);
                s.state = State::kWaiting;
                break;
            case State::kWaiting:
                if (!all_vikings_here(world, *this, s.missing, 48)) {
                    world.animate(*this);
                    return;
                }
                if (s.missing) {  // not all three: the level starts again
                    world.next_scene = world.scene;
                    world.leave = true;
                    return;
                }
                if (!talk_here(world)) {
                    world.level_done = true;
                    return;
                }
                for (int viking = 0; viking < kVikingSlots; ++viking) {
                    world.tell(viking, message::kConversation);
                }
                target = slot::kOlaf;
                s.state = State::kTalking;
                return;
            case State::kTalking:
                world.sound(*this, effect::kTalk);
                talk(world);
                s.state = State::kSaid;
                return;
            case State::kSaid:
                world.level_done = true;
                return;
        }
        world.animate(*this);
    }

  private:
    enum class State : std::uint8_t { kStart, kWaiting, kTalking, kSaid };
    struct Saved {
        bool missing = false;
        State state = State::kStart;
    } s;
    LVR_SAVES(Entity)

    static bool talk_here(const World& world) { return level::ends_a_world(world.level); }

    // Each named Viking is its target while he speaks.
    void talk(World& world) {
        for (const talk::Said& said : rows_of(talk::kWorldEndings, world.level)) {
            target = talk::slot_of(said.who);
            speak(world, said);
        }
    }
};

// Someone to talk to: the first time the Viking being played comes up to him (standing, well in view), and
// later whenever a Viking asks, they have their conversation.
class Talker : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            acts_as = tile::kThing;
            world.play(*this, anim::kTalkerStands);
            world.animate(*this);
            return;
        }
        if ((world.hints_seen & argument) == argument) {
            if (was_asked(*this)) {
                talk(world);
            }
        } else {
            if (viking_comes_near(world, *this, 30, -5)) {
                talk(world);
            }
        }
        world.animate(*this);
    }

  private:
    void talk(World& world) {
        const Entity* viking = target >= 0 && target < world.slots() ? world.at(target) : nullptr;
        if (viking == nullptr || viking->is_falling() || !world.can_be_spoken_to(*viking) ||
            !world.well_in_view(*this)) {
            return;
        }
        world.sound(*this, effect::kTalk);
        speak(world, talk::kEgyptian, this, target);
        world.hints_seen |= argument;
        message = 0;
    }
};

}  // namespace

std::unique_ptr<Entity> make_hint(std::uint16_t kind) {
    return make_entity<Hint>(kind == kind::kHint);
}

std::unique_ptr<Entity> make_level_start() {
    return make_entity<LevelStart>();
}

// The ones to talk to, and the level's exit and end: the kinds of this file, by number. (The prehistoric world has
// its two under numbers that are other things elsewhere.)
std::unique_ptr<Entity> make_conversation(std::uint16_t kind, int database) {
    if (database == database::kPrehistoric && kind == kind::kCaveman) {
        return make_entity<Caveman>();
    }
    if (database == database::kPrehistoric && kind == kind::kElder) {
        return make_entity<Elder>();
    }
    switch (kind) {
        case kind::kExit:
            return make_entity<Exit>();
        case kind::kLevelEnd:
            return make_entity<LevelEnd>();
        case kind::kShipStop:
            return database == database::kSpaceship ? make_entity<ShipStop>() : nullptr;
        case kind::kRemark:
            return make_entity<Remark>();
        case kind::kPrisoner:
            return make_entity<Prisoner>();
        case kind::kJanitor:
            return make_entity<Janitor>();
        case kind::kMechanic:
            return make_entity<Mechanic>();
        default:
            return nullptr;
    }
}

std::span<const KindOf> talkers() {
    static constexpr KindOf kList[] = {
        {Home::kEgypt, egypt_kind::kTalker, make<Talker>},
    };
    return kList;
}

}  // namespace lvr
