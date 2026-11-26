#pragma once

// The Modern visuals' effects (Remaster rules, ADR-0005; docs/ROADMAP.md, M8): what the engine shows gives off
// light and particles. The game never reads any of it; it is laid into and over its pictures, in the game's own
// pixels. Which animations do what is the table in effects.cpp, by the names animations.hpp gives them (what an
// entity was last started on: Sprite::started); which of a world's colours give light, by its database.

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include "assets/graphics.hpp"
#include "lvr/render.hpp"

namespace lvr {

class Entity;
class World;

class Effects {
  public:
    Effects();

    // What a picture of `world` (its scene's pictures `scene`), drawn from `view`, has of them: lights and the
    // colours that give light in `screen`, the particles as sprites after the view's own (drawn over them, under
    // what is in front). With `moving` (a picture of the play going on, not one made again going back, nor one of a
    // scene fading) the particles move on a picture's worth, what gives them off gives off more, and the lights
    // flicker; else all of it holds still. The lights are as bright as they are (a fade darkens them with the
    // picture: darken, render.hpp).
    void add(const World& world, const SceneGraphics& scene, View& view, Screen& screen, bool moving);
    // The particles there are go: the play was brought back, or another scene started.
    void clear() noexcept { particles_.clear(); }

  private:
    struct Particle {
        float x = 0;  // in the level
        float y = 0;
        float speed_x = 0;  // pixels a picture
        float speed_y = 0;
        float fall = 0;  // what is added to speed_y a picture
        int age = 0;     // pictures
        int life = 0;
        enum class Look : std::uint8_t { kSpark, kEmber, kSmoke, kTwirl } look = Look::kSpark;
        // Twirling (a teleport's): round orbit_x at `radius`, `angle` on by `turn` a picture, the radius by `widen`
        // (none: it flies as its speed says).
        float orbit_x = 0;
        float radius = 0;
        float angle = 0;
        float turn = 0;
        float widen = 0;
    };
    std::vector<Particle> particles_;
    // A few sparks flying out from (x, y) in the level.
    void spark(float x, float y);
    static std::array<float, 2> fuse_of(const Entity& bomb);
    void twirl(float x, float top, float bottom, bool in);
    // A blast going off at (x, y), `size` across: embers flying out all round, smoke, sparks.
    void blast(float x, float y, float size);
    std::vector<const Entity*> blasting_;  // what was a blast in the last picture (one new to it goes off)
    // What stops light, and the map's tile types it was made from (made anew when they change).
    std::shared_ptr<const Ground> ground_;
    std::vector<std::uint8_t> ground_of_;
    std::uint64_t grounds_made_ = 0;
    void make_ground(const World& world);
    // The colours the map's electricity is drawn in (its light is cast, not a glow), and of which scene's pictures.
    std::array<bool, 256> electric_colours_{};
    std::uint32_t electric_of_ = 0;
    void find_electric_colours(const World& world, const SceneGraphics& scene);
    // The same every run, on purpose: the same play shows the same effects (and a test can count on them).
    std::minstd_rand random_{kSeed};  // NOLINT(bugprone-random-generator-seed): (above)
    static constexpr std::uint_fast32_t kSeed = 1;
    std::array<assets::IndexedImage, 16> dots_;  // a pixel of each of the colours 0-15, for the particles
};

}  // namespace lvr
