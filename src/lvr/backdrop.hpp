#pragma once

// Parallax (Remaster rules): the SNES version's backdrop behind a level's map, where the DOS game shows black
// (docs/formats/snes.md, "The backdrops" and "How the game moves the backdrop"). Which pixels of the map show it,
// and where it is for a camera. The backdrop itself is the user's ROM image's (assets::load_level_backdrop); a scene
// has one only under Remaster rules and with the image there (load_slot), and only the GPU renderer draws it.

#include <cstdint>

#include "assets/graphics.hpp"

namespace lvr {

// A scene's backdrop: its picture, in the level's palette indices, and how it moves.
struct Backdrop {
    assets::IndexedImage image;
    // Per axis, in 256ths of a pixel: how far it moves per pixel of the camera, or with bit 15 by itself per
    // refresh (the spaceship's stars), whatever the camera does.
    std::uint16_t scroll_x = 0;
    std::uint16_t scroll_y = 0;
};

// Whether a pixel of the map shows the backdrop: colour 0 of one of the palette rows 0-7. The DOS level palette's
// rows 0-7 are the SNES level's colours, and a row's colour 0 is where a SNES tile shows what is behind it; the DOS
// palettes have it black in every one of those rows. The backdrop's own pixels are colours of those rows too, so
// they go in as they are, with the level's palette. (Sprites and text never use these colours.)
constexpr bool shows_backdrop(std::uint8_t colour) noexcept {
    constexpr std::uint8_t kRows = 0x80;  // rows 0-7
    constexpr std::uint8_t kColumn = 0x0f;
    return colour < kRows && (colour & kColumn) == 0;
}

// On one axis, where in the backdrop the view's first pixel is: the camera times the scroll word's speed in 256ths
// of a pixel (0x40 a quarter, 0x100 with the camera), or, with the word's bit 15, the backdrop's own movement
// (`refreshes` since the scene's start times the other 15 bits; the spaceship's stars), whatever the camera does.
// The backdrop repeats, so it is from 0 up to `size` (0 for a size of 0). The camera is the one without the screen
// shake, as the SNES game has it. Whole pixels of the camera and refreshes give what the SNES game shows once
// rounded down; between them (smooth movement) it moves on evenly.
double backdrop_offset(std::uint16_t scroll, double camera, double refreshes, int size) noexcept;

}  // namespace lvr
