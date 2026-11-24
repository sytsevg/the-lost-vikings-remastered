// The scenes of the village: the night the Vikings are taken, and their homecoming.

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "lvr/cues.hpp"
#include "lvr/dialogue.hpp"
#include "lvr/enemies.hpp"
#include "lvr/hits.hpp"
#include "lvr/kinds.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

// The night the Vikings are taken: a word of warning, then the alien ship (the entity in slot 2) comes over the
// village and stops over one house after another; the scene's switches tell the others what happens: 1, 2 and 4
// a Viking is pulled up, 8 the view moves on with the ship.
class Abduction : public Entity {
  public:
    void update(World& world) override {
        if (countdown != 0) {
            world.animate(*this);
            return;
        }
        constexpr int kShip = 2;
        constexpr int kLongShake = 255 * 4;
        static constexpr std::array<Cue, 53> kCues{{
            cue::effect(effect::kAbductionShip),
            cue::wait(0),
            cue::colour(text_colour::kErik),
            cue::say_at_cell(0x162, tail::kNone, 10, 2),
            cue::wait(80),
            cue::clear(),
            cue::wait(0),
            cue::stop_effect(effect::kAbductionShip),
            cue::wait(0),
            cue::effect(effect::kStarfieldSound),
            cue::shake(1, kLongShake),
            cue::speed_across(kShip, speed(2)),
            cue::wait(152),
            cue::effect(effect::kAbductionBeam),
            cue::switches(1),
            cue::wait(96),
            cue::stop_effect(effect::kAbductionBeam),
            cue::switches(0),
            cue::shake(1, kLongShake),
            cue::speed_across(kShip, speed(2)),
            cue::wait(63),
            cue::effect(effect::kAbductionBeam),
            cue::switches(2),
            cue::wait(96),
            cue::stop_effect(effect::kAbductionBeam),
            cue::switches(0),
            cue::shake(1, kLongShake),
            cue::own_speed(speed(2)),
            cue::speed_across(kShip, speed(5)),
            cue::wait(1),
            cue::own_speed(speed(6)),
            cue::wait(7),
            cue::switches(8),
            cue::wait(29),
            cue::effect(effect::kAbductionBeam),
            cue::own_speed(0),
            cue::speed_across(kShip, 0),
            cue::switches(4),
            cue::wait(96),
            cue::stop_effect(effect::kAbductionBeam),
            cue::switches(0),
            cue::shake(1, kLongShake),
            cue::speed_across(kShip, speed(2)),
            cue::wait(12),
            cue::shake(1, kLongShake),
            cue::stop_effect(effect::kStarfieldSound),
            cue::speed_down(kShip, speed(-3)),
            cue::speed_across(kShip, speed(3)),
            cue::wait(25),
            cue::wait(10),
            cue::shake(0, 0),
            cue::wait(85),
            cue::leave(),
        }};
        (void)run_cues(world, *this, kCues, s.at);
    }

  private:
    struct Saved {
        std::size_t at = 0;
    } s;
    LVR_SAVES(Entity)
};

// A Viking asleep in his house: when the scene's switch for him is on he is pulled up.
class TakenViking : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            never_again(world, *this);
            if (argument <= 2) {
                s.bit = static_cast<std::uint16_t>(1U << argument);
                world.play(*this, anim::kSceneLights.at(argument));
            }
        }
        if ((world.switches & s.bit) == s.bit) {
            speed_y = px(-2);
        }
        world.animate(*this);
    }

  private:
    struct Saved {
        std::uint16_t bit = 0;
    } s;
    LVR_SAVES(Entity)
};

// The alien ship: while one of the switches 1, 2, 4 is on it stops and its beam is on.
class AlienShip : public Entity {
  public:
    void update(World& world) override {
        const bool beam = (world.switches & 7U) != 0;
        switch (s.state) {
            case State::kStart:
                world.play(*this, anim::kAlienShipFlies);
                world.animate(*this);
                s.state = State::kFlying;
                [[fallthrough]];
            case State::kFlying:
                world.animate(*this);
                if (beam) {
                    s.state = State::kStopping;
                }
                return;
            case State::kStopping:
                speed_x = 0;
                speed_y = 0;
                world.play(*this, anim::kAlienShipBeams);
                s.state = State::kBeaming;
                [[fallthrough]];
            case State::kBeaming:
                if (!beam) {
                    world.play(*this, anim::kAlienShipBeamOff);
                    s.state = State::kFlying;
                }
                world.animate(*this);
                return;
        }
    }

  private:
    enum class State : std::uint8_t { kStart, kFlying, kStopping, kBeaming };

    struct Saved {
        State state = State::kStart;
    } s;
    LVR_SAVES(Entity)
};

// A light of the beam: unseen for a while (its argument's upper bits, eight a tick), then shown until its switch
// is on; then its animation plays out and it is gone.
class BeamLight : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            world.play(*this, anim::kBeamLightLooks);
            s.delay = argument & 0xfff8U;
            argument &= 7U;
            world.animate(*this);
            world.hide_pieces(*this);
        }
        if (s.hidden) {
            if (s.delay != 0) {
                s.delay -= 8;
                return;
            }
            s.hidden = false;
            world.show_pieces(*this);
        }
        if (!s.ending && switches_on(world, argument)) {
            s.ending = true;
        }
        if (s.ending && animation_over()) {
            world.remove(slot);
            return;
        }
        world.animate(*this);
    }

  private:
    struct Saved {
        bool hidden = true;
        bool ending = false;
        std::uint16_t delay = 0;
    } s;
    LVR_SAVES(Entity)
};

// Something of the night village that moves on to the right while the scene's switch 8 is on.
class NightThing : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            if (kind == scene_kind::kNightThing) {
                world.play(*this, anim::kNightThingLooks);
            } else if (argument == 0) {
                world.play(*this, anim::kNightOtherLooks.at(0));
            } else if (argument == 1) {
                world.play(*this, anim::kNightOtherLooks.at(1));
            }
        }
        speed_x = static_cast<std::int16_t>((world.switches & 8U) != 0 ? px(2) : 0);
        world.animate(*this);
    }
};

// The village at the start of the game: each Viking, the first time he is the one played with the view at rest,
// has his say (Erik's ends with a hint); when all three have, it is gone.
class VillageVoices : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            return;
        }
        if (countdown != 0) {
            return;
        }
        if (run_cues(world, *this, std::span{s.cues}.first(s.cue_count), s.at)) {
            return;
        }
        if (s.talking >= 0) {  // he has had his say
            s.said.at(static_cast<std::size_t>(s.talking)) = true;
            s.talking = -1;
            if (s.said[0] && s.said[1] && s.said[2]) {
                gone_for_good(world, *this);
            }
            return;
        }
        for (int viking = 0; viking < 3; ++viking) {
            if (s.said.at(static_cast<std::size_t>(viking))) {
                continue;
            }
            target = viking;
            const Entity* he = world.at(viking);
            constexpr int kAtRest = 4;
            if (he == nullptr || world.current_viking != viking ||
                static_cast<std::uint16_t>(world.camera.moved_x) >= kAtRest ||
                static_cast<std::uint16_t>(world.camera.moved_y) >= kAtRest) {
                if (viking == 2) {
                    return;
                }
                continue;
            }
            const int px = he->x;
            s.cue_count = 0;
            const auto add = [this](const Cue& cue) { s.cues.at(s.cue_count++) = cue; };
            add(cue::effect(effect::kTalk));
            if (viking == 0) {
                const int py = he->y - 52;
                add(cue::colour(text_colour::kErik));
                add(cue::say_at_point(0x15b, tail::kBelowLeft, px, py));
                add(cue::wait(120));
                add(cue::clear());
                add(cue::say_at_point(0x15c, tail::kBelowLeft, px, py));
                add(cue::wait(120));
                add(cue::clear());
                add(cue::colour(text_colour::kOthers));
                add(cue::say_at_point(0x178, tail::kBelowMiddle, px + 32, py));
                add(cue::wait(80));
                add(cue::clear());
            } else if (viking == 1) {
                const int py = he->y - 48;
                add(cue::colour(text_colour::kBaleog));
                add(cue::say_at_point(0x15d, tail::kBelowMiddle, px, py));
                add(cue::wait(120));
                add(cue::clear());
            } else {
                const int py = he->y - 48;
                add(cue::colour(text_colour::kOlaf));
                add(cue::say_at_point(0x15e, tail::kBelowMiddle, px, py));
                add(cue::wait(80));
                add(cue::clear());
                add(cue::say_at_point(0x15f, tail::kBelowMiddle, px, py));
                add(cue::wait(80));
                add(cue::clear());
            }
            s.at = 0;
            s.talking = viking;
            (void)run_cues(world, *this, std::span{s.cues}.first(s.cue_count), s.at);
            return;
        }
    }

  private:
    struct Saved {
        std::array<bool, 3> said{};
        int talking = -1;
        std::array<Cue, 12> cues{};  // what the Viking spoken to says, at most this much
        std::size_t cue_count = 0;   // how much of it there is
        std::size_t at = 0;
    } s;
    LVR_SAVES(Entity)
};

// Something in the village a Viking can act on: he says two things about it, a hint follows, and the first item
// of each Viking is taken away.
class VillageThing : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            return;
        }
        if (countdown != 0) {
            return;
        }
        static constexpr std::array<Cue, 12> kCues{{
            cue::effect(effect::kTalk),
            cue::colour(text_colour::kErik),
            cue::say(0x160, tail::kBelowMiddle, -40),
            cue::wait(80),
            cue::clear(),
            cue::say(0x161, tail::kBelowMiddle, -40),
            cue::wait(80),
            cue::clear(),
            cue::colour(text_colour::kOthers),
            cue::say_at_entity(0x179, tail::kBelowRight, 32, -40),
            cue::wait(80),
            cue::clear(),
        }};
        if (!s.touched) {
            if (saw.viking == kNobody || (world.pressed & key::kS) == 0) {
                return;
            }
            target = saw.viking;
            s.touched = true;
        }
        if (run_cues(world, *this, kCues, s.at)) {
            return;
        }
        for (auto& his : world.items) {
            his[0] = 0;
        }
        world.remove(slot);
    }
    Looks looks_for() const override { return Looks{touching_any(hit::kVikingInField, &Saw::viking)}; }

  private:
    struct Saved {
        bool touched = false;
        std::size_t at = 0;
    } s;
    LVR_SAVES(Entity)
};

// The village the Vikings come home to: they fall in, Olaf has a word, they walk on, the village greets them,
// more words, and they cheer.
class Homecoming : public Entity {
  public:
    void update(World& world) override {
        if (countdown != 0) {
            return;
        }
        static constexpr std::array<Cue, 38> kCues{{
            cue::health(6),
            cue::wait(100),
            cue::effect(effect::kTalk),
            cue::colour(text_colour::kOlaf),
            cue::say_at(2, 0x16a, tail::kBelowLeft, -40),
            cue::wait(120),
            cue::clear(),
            cue::tell(message::kLedWalk),
            cue::wait(72),
            cue::tell(message::kLedStand),
            cue::effect(effect::kTalk),
            cue::colour(text_colour::kOthers),
            cue::say_at_cell(0x163, tail::kBelowRight, 28, 5),
            cue::wait(120),
            cue::clear(),
            cue::say_at_cell(0x164, tail::kBelowRight, 28, 1),
            cue::wait(20),
            cue::say_at_cell(0x165, tail::kBelowRight, 20, 4),
            cue::wait(20),
            cue::say_at_cell(0x166, tail::kBelowRight, 23, 7),
            cue::wait(120),
            cue::clear(),
            cue::wait(30),
            cue::colour(text_colour::kErik),
            cue::say_at(0, 0x167, tail::kBelowMiddle, -40),
            cue::wait(200),
            cue::clear(),
            cue::say_at(0, 0x168, tail::kBelowMiddle, -40),
            cue::wait(200),
            cue::clear(),
            cue::wait(30),
            cue::colour(text_colour::kOlaf),
            cue::say_at(2, 0x169, tail::kBelowLeft, -40),
            cue::wait(200),
            cue::clear(),
            cue::tell(message::kLedCheer),
            cue::wait(120),
            cue::leave(),
        }};
        (void)run_cues(world, *this, kCues, s.at);
    }

  private:
    struct Saved {
        std::size_t at = 0;
    } s;
    LVR_SAVES(Entity)
};

// Someone or something of the village that only plays its animation: which, by its argument.
class Villager : public Entity {
  public:
    void update(World& world) override {
        if (first_turn()) {
            // By argument; one without an animation there shows nothing.
            const auto& all = kind == scene_kind::kVillager ? anim::kVillagerLooks : anim::kOtherVillagerLooks;
            const Animation looks = argument < all.size() ? all.at(argument) : Animation{};
            if (looks.place != 0) {
                world.play(*this, looks);
            }
        }
        world.animate(*this);
    }
};

}  // namespace

std::span<const KindOf> village_scenes() {
    static constexpr KindOf kList[] = {
        {Home::kScenes, scene_kind::kAbduction, make<Abduction>},
        {Home::kScenes, scene_kind::kTakenViking, make<TakenViking>},
        {Home::kScenes, scene_kind::kAlienShip, make<AlienShip>},
        {Home::kScenes, scene_kind::kBeamLight, make<BeamLight>},
        {Home::kScenes, scene_kind::kNightThing, make<NightThing>},
        {Home::kScenes, scene_kind::kNightThingE1, make<NightThing>},
        {Home::kScenes, scene_kind::kVillageVoices, make<VillageVoices>},
        {Home::kScenes, scene_kind::kVillageThing, make<VillageThing>},
        {Home::kScenes, scene_kind::kHomecoming, make<Homecoming>},
        {Home::kScenes, scene_kind::kVillager, make<Villager>},
        {Home::kScenes, scene_kind::kVillagerE6, make<Villager>},
    };
    return kList;
}

}  // namespace lvr
