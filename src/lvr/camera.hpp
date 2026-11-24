#pragma once

// The camera: the view's top-left corner in the level. It follows one entity (the Viking being played): while that
// entity is inside a box in the middle of the view the camera stays; outside it, the camera goes after it by how
// far it is out, at most 16 pixels a tick, taken over the tick's three pictures. Checked against the reference
// engine's (docs/original/camera.md).

#include "lvr/level.hpp"

namespace lvr {

struct Camera {
    int x = 0;
    int y = 0;
    int max_x = 0;  // the furthest it goes: the map's size less the view's
    int max_y = 0;
    bool locked_x = false;  // it doesn't move across ...
    bool locked_y = false;  // ... or down
    // This tick's move: how far the followed entity was out of the box when the tick began, one way per axis ...
    int out_left = 0;
    int out_right = 0;
    int out_up = 0;
    int out_down = 0;
    // ... and how far the last picture's share moved the camera.
    int moved_x = 0;
    int moved_y = 0;

    bool operator==(const Camera&) const = default;
};

inline constexpr int kViewWidth = 320;
inline constexpr int kViewHeight = 176;

// The limits for a map of this size, in cells.
void set_camera_limits(Camera& camera, int map_width, int map_height) noexcept;

// At a level's start: the entity at (x, y) in the middle of the view, within the limits.
void start_camera(Camera& camera, int x, int y) noexcept;

// One picture's share of following the entity at (x, y). Picture 0 of a tick measures how far it is out of the box
// (144-176 across, 80-96 down, from the view's corner); pictures 1 and 2 take the rest of that.
void follow(Camera& camera, int x, int y, int picture) noexcept;

}  // namespace lvr
