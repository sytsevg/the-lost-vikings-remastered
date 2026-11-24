#pragma once

// Indexed PNG load/save through SDL3's built-in PNG support (SDL 3.4+). Pixels stay palette indices and the
// palette travels inside the file (ADR-0014), so palette effects can be reapplied after loading.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "assets/graphics.hpp"

namespace lvr::image {

// Writes an 8-bit indexed PNG with the full 256-colour palette. `transparent` marks one palette index as fully
// transparent (written as PNG palette alpha, e.g. colour 0 for sprites). Deterministic: same input, same bytes.
std::expected<void, std::string> save_indexed_png(const std::filesystem::path& path, const assets::IndexedImage& image,
                                                  const assets::Palette& palette,
                                                  std::optional<std::uint8_t> transparent = {});

struct IndexedPng {
    assets::IndexedImage image;
    assets::Palette palette;                  // entries the file doesn't define are black
    std::optional<std::uint8_t> transparent;  // the first fully transparent palette index, if any
};

// Reads an 8-bit indexed PNG. Fails for truecolour PNGs.
std::expected<IndexedPng, std::string> load_indexed_png(const std::filesystem::path& path);

}  // namespace lvr::image
