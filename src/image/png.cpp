#include "image/png.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <memory>
#include <span>

namespace lvr::image {

namespace {

struct SurfaceDeleter {
    void operator()(SDL_Surface* surface) const noexcept { SDL_DestroySurface(surface); }
};
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

std::string sdl_error(std::string_view what, const std::filesystem::path& path) {
    return std::format("{} {}: {}", what, path.string(), SDL_GetError());
}

}  // namespace

std::expected<void, std::string> save_indexed_png(const std::filesystem::path& path, const assets::IndexedImage& image,
                                                  const assets::Palette& palette,
                                                  std::optional<std::uint8_t> transparent) {
    const SurfacePtr surface{
        SDL_CreateSurface(static_cast<int>(image.width), static_cast<int>(image.height), SDL_PIXELFORMAT_INDEX8)};
    if (!surface) {
        return std::unexpected{sdl_error("cannot create surface for", path)};
    }
    SDL_Palette* sdl_palette = SDL_CreateSurfacePalette(surface.get());
    if (sdl_palette == nullptr) {
        return std::unexpected{sdl_error("cannot create palette for", path)};
    }
    std::array<SDL_Color, 256> colours{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        colours[i] = SDL_Color{.r = palette[i].r, .g = palette[i].g, .b = palette[i].b, .a = SDL_ALPHA_OPAQUE};
    }
    if (transparent) {
        colours[*transparent].a = SDL_ALPHA_TRANSPARENT;
    }
    if (!SDL_SetPaletteColors(sdl_palette, colours.data(), 0, static_cast<int>(colours.size()))) {
        return std::unexpected{sdl_error("cannot set palette for", path)};
    }

    // Rows may be padded (pitch >= width).
    const std::span<std::byte> pixels{static_cast<std::byte*>(surface->pixels),
                                      static_cast<std::size_t>(surface->pitch) * image.height};
    for (std::size_t y = 0; y < image.height; ++y) {
        std::memcpy(pixels.subspan(y * static_cast<std::size_t>(surface->pitch)).data(),
                    image.pixels.data() + (y * image.width), image.width);
    }

    if (!SDL_SavePNG(surface.get(), path.string().c_str())) {
        return std::unexpected{sdl_error("cannot write", path)};
    }
    return {};
}

std::expected<IndexedPng, std::string> load_indexed_png(const std::filesystem::path& path) {
    const SurfacePtr surface{SDL_LoadPNG(path.string().c_str())};
    if (!surface) {
        return std::unexpected{sdl_error("cannot read", path)};
    }
    const SDL_Palette* sdl_palette = SDL_GetSurfacePalette(surface.get());
    if (surface->format != SDL_PIXELFORMAT_INDEX8 || sdl_palette == nullptr) {
        return std::unexpected{std::format("{} is not an 8-bit indexed PNG", path.string())};
    }

    IndexedPng result{
        .image = assets::make_image(static_cast<std::size_t>(surface->w), static_cast<std::size_t>(surface->h)),
        .palette = {},
        .transparent = {}};
    const std::span<const std::byte> pixels{static_cast<const std::byte*>(surface->pixels),
                                            static_cast<std::size_t>(surface->pitch) * result.image.height};
    for (std::size_t y = 0; y < result.image.height; ++y) {
        std::memcpy(result.image.pixels.data() + (y * result.image.width),
                    pixels.subspan(y * static_cast<std::size_t>(surface->pitch)).data(), result.image.width);
    }
    const std::span<const SDL_Color> colours{sdl_palette->colors, static_cast<std::size_t>(sdl_palette->ncolors)};
    for (std::size_t i = 0; i < colours.size() && i < result.palette.size(); ++i) {
        result.palette[i] = assets::Rgb{.r = colours[i].r, .g = colours[i].g, .b = colours[i].b};
        if (colours[i].a == SDL_ALPHA_TRANSPARENT && !result.transparent) {
            result.transparent = static_cast<std::uint8_t>(i);
        }
    }
    return result;
}

}  // namespace lvr::image
