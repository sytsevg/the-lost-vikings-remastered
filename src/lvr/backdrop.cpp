#include "lvr/backdrop.hpp"

#include <algorithm>
#include <cmath>

namespace lvr {

double backdrop_offset(std::uint16_t scroll, double camera, double refreshes, int size) noexcept {
    constexpr std::uint16_t kByItself = 0x8000;
    constexpr double kFraction = 256;
    if (size <= 0) {
        return 0;
    }
    const double speed = static_cast<double>(scroll & static_cast<std::uint16_t>(~kByItself)) / kFraction;
    const double moved = ((scroll & kByItself) != 0 ? std::max(refreshes, 0.0) : std::max(camera, 0.0)) * speed;
    return std::fmod(moved, static_cast<double>(size));
}

}  // namespace lvr
