#pragma once

// A development tool (vikings-remaster --showcase): places in the levels where the Modern visuals' effects can be
// seen (effects.hpp), one after another, each with the view held on it while the level plays. No level has all of
// them (a world's kinds and pictures are its own), so this is the "level" that has: a list of real ones' spots.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "lvr/session.hpp"

namespace lvr {

struct Scenario {
    std::string_view name;
    int slot = 0;  // the level's scene
    int x = 0;     // the view's corner in the level
    int y = 0;
};

// The view a scenario is seen in: a little larger than the original's.
inline constexpr int kShowcaseWidth = 384;
inline constexpr int kShowcaseHeight = 224;

inline constexpr std::array kScenarios{
    Scenario{.name = "spaceship: laser shots from wall guns", .slot = 1, .x = 0, .y = 240},
    Scenario{.name = "spaceship: laser shots and an electric barrier", .slot = 3, .x = 896, .y = 400},
    Scenario{.name = "factory: a turret's laser shots", .slot = 21, .x = 500, .y = 590},
    Scenario{.name = "spaceship: an electric barrier across a corridor", .slot = 3, .x = 730, .y = 328},
    Scenario{.name = "spaceship: screens, lights and electricity", .slot = 3, .x = 0, .y = 0},
    Scenario{.name = "spaceship: electricity along a floor", .slot = 34, .x = 360, .y = 680},
    Scenario{.name = "spaceship: electricity up a wall", .slot = 34, .x = 900, .y = 340},
    Scenario{.name = "spaceship: an electric field", .slot = 33, .x = 312, .y = 96},
    Scenario{.name = "Tomator's level: electric barriers, electricity and lights", .slot = 36, .x = 240, .y = 200},
    Scenario{.name = "prehistoric: fireballs", .slot = 10, .x = 0, .y = 536},
    Scenario{.name = "prehistoric: arcing fire", .slot = 10, .x = 576, .y = 30},
    Scenario{.name = "factory: a fire jet", .slot = 19, .x = 584, .y = 64},
    Scenario{.name = "factory: an electric barrier", .slot = 20, .x = 0, .y = 904},
    Scenario{.name = "wacky world: fire jets", .slot = 25, .x = 824, .y = 192},
    Scenario{.name = "wacky world: flames", .slot = 28, .x = 980, .y = 184},
    Scenario{.name = "wacky world: an electric barrier", .slot = 29, .x = 0, .y = 24},
};

// Takes a session to the scenarios: to the level, past what waits for a key there (its password, a text: the
// showcase presses F), and holds the view on the spot. Keys the player gives are passed on once it is there.
class Showcase {
  public:
    explicit Showcase(std::size_t first) : index_{first % kScenarios.size()} {}
    const Scenario& current() const { return kScenarios.at(index_); }
    // "Showcase 3/13: ...", and the keys that go on.
    std::string title() const;
    // To the scenario `by` on (or back) from this one.
    void next(int by);
    // Before a step: the keys to give it.
    std::uint16_t before_step(Session& session, std::uint16_t keys);

  private:
    std::size_t index_ = 0;
    bool held_ = false;
    bool asked_ = false;  // the level asked for: it is on its way
    int steps_ = 0;
};

}  // namespace lvr
