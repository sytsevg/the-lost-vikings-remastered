#include "lvr/camera.hpp"

#include <algorithm>
#include <cstdint>

#include "lvr/sprites.hpp"

namespace lvr {

namespace {

// The box in the view's middle that the followed entity can move in without the camera moving: this far either
// side of the middle.
constexpr int kBoxAcross = 16;
constexpr int kBoxDown = 8;
constexpr int kMostPerTick = 16;

// Positions are 16-bit words in the original; a level is far smaller, but a map narrower than the view gives a
// "negative" limit that the original compares as a large number.
int wrapped(int value) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}
unsigned as_unsigned(int value) noexcept {
    return static_cast<std::uint16_t>(value);
}

// How far `a` is past `b`, at most a tick's 16; 0 if it isn't.
int past(int a, int b) noexcept {
    a = wrapped(a);
    b = wrapped(b);
    if (a <= b) {
        return 0;
    }
    const int amount = wrapped(a - b);
    return amount < kMostPerTick ? amount : kMostPerTick;
}

// Back towards 0 by `step`, not past it.
void back(int& position, int& moved, bool locked, int step) noexcept {
    if (locked) {
        return;
    }
    const int old = position;
    position = std::max(wrapped(old - step), 0);
    moved = wrapped(old - position);
}

// On towards the limit by `step`, not past it.
void on(int& position, int& moved, bool locked, int step, int limit) noexcept {
    if (locked) {
        return;
    }
    const int old = position;
    position = wrapped(old + step);
    if (as_unsigned(position) >= as_unsigned(limit)) {
        position = limit;
    }
    moved = wrapped(position - old);
}

int start(int position, int half_view, int limit) noexcept {
    position = wrapped(position);
    const int at = position >= half_view ? position - half_view : 0;
    return at > wrapped(limit) ? limit : at;
}

}  // namespace

void set_camera_limits(Camera& camera, int map_width, int map_height) noexcept {
    camera.max_x = wrapped((map_width * kCellSize) - camera.view_width);
    camera.max_y = wrapped((map_height * kCellSize) - camera.view_height + camera.covered);
}

void start_camera(Camera& camera, int x, int y) noexcept {
    camera.x = start(x, camera.view_width / 2, camera.max_x);
    camera.y = start(y, (camera.view_height - camera.covered) / 2, camera.max_y);
}

void follow(Camera& camera, int x, int y, int picture) noexcept {
    if (picture == 0) {
        camera.moved_x = camera.moved_y = 0;
        const int middle_x = camera.view_width / 2;
        const int middle_y = (camera.view_height - camera.covered) / 2;
        camera.out_left = past(camera.x + middle_x - kBoxAcross, x);
        camera.out_right = camera.out_left != 0 ? 0 : past(x - camera.x, middle_x + kBoxAcross);
        camera.out_up = past(camera.y + middle_y - kBoxDown, y);
        camera.out_down = camera.out_up != 0 ? 0 : past(y - camera.y, middle_y + kBoxDown);
    }
    // The same split as a moving sprite's: 7 is 3, 2, 2.
    if (camera.out_left != 0) {
        back(camera.x, camera.moved_x, camera.locked_x, share_of_move(camera.out_left, picture));
    } else if (camera.out_right != 0) {
        on(camera.x, camera.moved_x, camera.locked_x, share_of_move(camera.out_right, picture), camera.max_x);
    }
    if (camera.out_up != 0) {
        back(camera.y, camera.moved_y, camera.locked_y, share_of_move(camera.out_up, picture));
    } else if (camera.out_down != 0) {
        on(camera.y, camera.moved_y, camera.locked_y, share_of_move(camera.out_down, picture), camera.max_y);
    }
}

}  // namespace lvr
