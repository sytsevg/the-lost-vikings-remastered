#include "lvr/gpu.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lvr {

namespace {

// The shaders, compiled when the engine is built (gpu_quad.vert, gpu_quad.frag).
constexpr std::uint32_t kVertexShader[] =
#include "gpu_quad.vert.inc"
    ;
constexpr std::uint32_t kFragmentShader[] =
#include "gpu_quad.frag.inc"
    ;
constexpr std::uint32_t kShowVertexShader[] =
#include "gpu_show.vert.inc"
    ;
constexpr std::uint32_t kShowFragmentShader[] =
#include "gpu_show.frag.inc"
    ;

// How many pictures are kept for showing: more than two steps make (a change of scene is some 150), since the
// last of a step is still shown while the next step's are drawn.
constexpr std::size_t kKept = 512;
constexpr std::size_t kColours = 256;
constexpr int kHudHeight = kScreenHeight - kPlayfieldHeight;

constexpr int kAtlasSize = 2048;
constexpr int kTilesAcross = kAtlasSize / kTileSize;  // the tileset's tiles in the atlas, a row of them

struct Spot {
    int x = 0;
    int y = 0;
};

// One picture on the target, as the vertex shader takes it (gpu_quad.vert).
struct Quad {
    float left = 0, top = 0, width = 0, height = 0;
    float source_x = 0, source_y = 0, mirrored = 0, flipped = 0;
    float mask_dx = 0, mask_dy = 0, see_through = 0, masked = 0;
};

// The atlas as the CPU keeps it: pictures placed in rows, each row as tall as its tallest. What is placed after a
// mark can be taken away again (the scene's pictures when the scene changes; the sprites' at every picture).
class Atlas {
  public:
    struct Mark {
        int x = 0;
        int y = 0;
        int row = 0;
    };

    Mark mark() const noexcept { return {.x = x_, .y = y_, .row = row_}; }
    void back_to(const Mark& mark) noexcept {
        x_ = mark.x;
        y_ = mark.y;
        row_ = mark.row;
    }

    // Room for `width` x `height` pixels; none when the atlas is full.
    std::optional<Spot> place(int width, int height) {
        if (width > kAtlasSize) {
            return std::nullopt;
        }
        if (x_ + width > kAtlasSize) {
            y_ += row_;
            x_ = row_ = 0;
        }
        if (y_ + height > kAtlasSize) {
            return std::nullopt;
        }
        const Spot spot{.x = x_, .y = y_};
        x_ += width;
        row_ = std::max(row_, height);
        if (height > 0) {
            changed_top_ = std::min(changed_top_, spot.y);
            changed_bottom_ = std::max(changed_bottom_, spot.y + height);
        }
        return spot;
    }

    std::optional<Spot> put(const assets::IndexedImage& image) {
        const auto spot = place(static_cast<int>(image.width), static_cast<int>(image.height));
        if (spot) {
            for (std::size_t row = 0; row < image.height; ++row) {
                std::memcpy(at(spot->x, spot->y + static_cast<int>(row)), &image.pixels[row * image.width],
                            image.width);
            }
        }
        return spot;
    }

    std::uint8_t* at(int x, int y) noexcept {
        return &pixels_[(static_cast<std::size_t>(y) * kAtlasSize) + static_cast<std::size_t>(x)];
    }

    // The rows written since the last time, and no longer after this.
    std::pair<int, int> take_changed() noexcept {
        return {std::exchange(changed_top_, kAtlasSize), std::exchange(changed_bottom_, 0)};
    }

  private:
    std::vector<std::uint8_t> pixels_ = std::vector<std::uint8_t>(std::size_t{kAtlasSize} * kAtlasSize);
    int x_ = 0;
    int y_ = 0;
    int row_ = 0;
    int changed_top_ = kAtlasSize;
    int changed_bottom_ = 0;
};

Quad quad(Spot from, int x, int y, int width, int height) {
    return Quad{.left = static_cast<float>(x),
                .top = static_cast<float>(y),
                .width = static_cast<float>(width),
                .height = static_cast<float>(height),
                .source_x = static_cast<float>(from.x),
                .source_y = static_cast<float>(from.y)};
}

std::unexpected<std::string> sdl_error(const char* what) {
    return std::unexpected{std::string{what} + ": " + SDL_GetError()};
}

}  // namespace

struct GpuRenderer::State {
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    SDL_GPUTexture* atlas_texture = nullptr;
    SDL_GPUTexture* target = nullptr;
    SDL_GPUBuffer* quads_buffer = nullptr;
    SDL_GPUTransferBuffer* atlas_upload = nullptr;
    SDL_GPUTransferBuffer* quads_upload = nullptr;
    SDL_GPUTransferBuffer* download = nullptr;
    // Showing a kept picture: its colours looked up, onto the window or (for a test) a target read back.
    SDL_Window* window = nullptr;
    SDL_GPUGraphicsPipeline* show_pipeline = nullptr;      // onto the window
    SDL_GPUGraphicsPipeline* show_pipeline_own = nullptr;  // onto `shown_target`
    SDL_GPUTexture* palette_texture = nullptr;
    SDL_GPUTransferBuffer* palette_upload = nullptr;
    // A kept picture: the playfield (`width` x `height`: the view's) with the HUD's rows under it.
    struct Kept {
        SDL_GPUTexture* texture = nullptr;
        int width = 0;
        int height = 0;
    };
    std::vector<Kept> kept = std::vector<Kept>(kKept);  // made as they are first used
    std::size_t kept_count = 0;                         // how many were drawn so far
    std::size_t quads_room = 0;                         // how many the buffers hold
    bool video_started = false;

    Atlas atlas;
    // What the atlas holds, and where.
    struct Shared {
        const SharedGraphics* of = nullptr;
        std::vector<Spot> font, portraits, health, items;
        Spot panel, picker_mask;
    } shared;
    struct Scene {
        std::uint32_t id = 0;
        bool placed = false;
        Spot tiles, masks, picture, hud_picture;
    } scene;
    Atlas::Mark after_shared, after_scene;
    // The sprites' pictures, each put into the atlas the first time it is shown and kept there while the scene's
    // pictures are: found again by what it looks like (a hash of its size and pixels).
    std::unordered_map<std::uint64_t, Spot> pieces;
    std::vector<Spot> piece_spots;  // this picture's sprites, in order
    bool place_pieces(const View& view);
    std::vector<Quad> quads;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        if (device != nullptr) {
            for (const Kept& picture : kept) {
                SDL_ReleaseGPUTexture(device, picture.texture);
            }
            SDL_ReleaseGPUTransferBuffer(device, palette_upload);
            SDL_ReleaseGPUTexture(device, palette_texture);
            SDL_ReleaseGPUGraphicsPipeline(device, show_pipeline_own);
            SDL_ReleaseGPUGraphicsPipeline(device, show_pipeline);
            if (window != nullptr) {
                SDL_ReleaseWindowFromGPUDevice(device, window);
            }
            SDL_ReleaseGPUTransferBuffer(device, download);
            SDL_ReleaseGPUTransferBuffer(device, quads_upload);
            SDL_ReleaseGPUTransferBuffer(device, atlas_upload);
            SDL_ReleaseGPUBuffer(device, quads_buffer);
            SDL_ReleaseGPUTexture(device, target);
            SDL_ReleaseGPUTexture(device, atlas_texture);
            SDL_ReleaseGPUSampler(device, sampler);
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
            SDL_DestroyGPUDevice(device);
        }
        if (video_started) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    std::expected<void, std::string> set_up();
    SDL_GPUTexture* texture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int width, int height);
    SDL_GPUTexture* picture_texture(int width, int height);
    std::expected<void, std::string> render(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* onto, const View& view,
                                            const SceneGraphics& graphics, const SharedGraphics& common);
    std::expected<void, std::string> show(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* onto, int width, int height,
                                          const Screen& screen, SDL_GPUGraphicsPipeline* with);
    bool place_shared(const SharedGraphics& graphics);
    bool place_scene(const SceneGraphics& graphics);
    bool list(const View& view, const SceneGraphics& graphics, std::size_t& in_playfield);
    void list_tiles(const View& view, const SceneGraphics& graphics, bool foreground);
    void list_hud(const HudView& hud, const SharedGraphics& graphics, int top);
    std::expected<void, std::string> room_for_quads();
};

std::expected<void, std::string> GpuRenderer::State::set_up() {
    const auto shader = [this](const std::uint32_t* code, std::size_t size, SDL_GPUShaderStage stage,
                               std::uint32_t samplers, std::uint32_t uniforms) {
        const SDL_GPUShaderCreateInfo info{.code_size = size,
                                           .code = reinterpret_cast<const Uint8*>(code),  // NOLINT(*-reinterpret-cast)
                                           .entrypoint = "main",
                                           .format = SDL_GPU_SHADERFORMAT_SPIRV,
                                           .stage = stage,
                                           .num_samplers = samplers,
                                           .num_storage_textures = 0,
                                           .num_storage_buffers = 0,
                                           .num_uniform_buffers = uniforms,
                                           .props = 0};
        return SDL_CreateGPUShader(device, &info);
    };
    SDL_GPUShader* vertex = shader(kVertexShader, sizeof(kVertexShader), SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* fragment = shader(kFragmentShader, sizeof(kFragmentShader), SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    if (vertex == nullptr || fragment == nullptr) {
        return sdl_error("the shaders");
    }
    const SDL_GPUVertexBufferDescription buffer{
        .slot = 0, .pitch = sizeof(Quad), .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE, .instance_step_rate = 0};
    constexpr std::uint32_t kFour = 4 * sizeof(float);
    const SDL_GPUVertexAttribute attributes[] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 0},
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = kFour},
        {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 2 * kFour}};
    SDL_GPUColorTargetDescription colour{};
    colour.format = SDL_GPU_TEXTUREFORMAT_R8_UINT;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.vertex_input_state = {.vertex_buffer_descriptions = &buffer,
                               .num_vertex_buffers = 1,
                               .vertex_attributes = attributes,
                               .num_vertex_attributes = 3};
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &colour;
    info.target_info.num_color_targets = 1;
    pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
    if (pipeline == nullptr) {
        return sdl_error("the pipeline");
    }

    SDL_GPUSamplerCreateInfo nearest{};
    nearest.min_filter = nearest.mag_filter = SDL_GPU_FILTER_NEAREST;
    nearest.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    nearest.address_mode_u = nearest.address_mode_v = nearest.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler = SDL_CreateGPUSampler(device, &nearest);

    atlas_texture = texture(SDL_GPU_TEXTUREFORMAT_R8_UINT, SDL_GPU_TEXTUREUSAGE_SAMPLER, kAtlasSize, kAtlasSize);
    target = picture_texture(kScreenWidth, kPlayfieldHeight);
    palette_texture = texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, kColours, 1);
    const auto transfer = [this](SDL_GPUTransferBufferUsage usage, std::size_t size) {
        const SDL_GPUTransferBufferCreateInfo made{.usage = usage, .size = static_cast<Uint32>(size), .props = 0};
        return SDL_CreateGPUTransferBuffer(device, &made);
    };
    atlas_upload = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, std::size_t{kAtlasSize} * kAtlasSize);
    download = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, std::size_t{kScreenWidth} * kScreenHeight);
    palette_upload = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, kColours * 4);
    if (sampler == nullptr || atlas_texture == nullptr || target == nullptr || atlas_upload == nullptr ||
        download == nullptr || palette_texture == nullptr || palette_upload == nullptr || false) {
        return sdl_error("the textures");
    }

    // Showing: the same two shaders onto the window's format and onto our own.
    SDL_GPUShader* show_vertex = shader(kShowVertexShader, sizeof(kShowVertexShader), SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* show_fragment =
        shader(kShowFragmentShader, sizeof(kShowFragmentShader), SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
    if (show_vertex == nullptr || show_fragment == nullptr) {
        return sdl_error("the shaders");
    }
    const auto show_onto = [this, show_vertex, show_fragment](SDL_GPUTextureFormat format) {
        SDL_GPUColorTargetDescription shown{};
        shown.format = format;
        SDL_GPUGraphicsPipelineCreateInfo made{};
        made.vertex_shader = show_vertex;
        made.fragment_shader = show_fragment;
        made.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        made.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        made.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        made.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        made.target_info.color_target_descriptions = &shown;
        made.target_info.num_color_targets = 1;
        return SDL_CreateGPUGraphicsPipeline(device, &made);
    };
    show_pipeline_own = show_onto(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    if (window != nullptr) {
        show_pipeline = show_onto(SDL_GetGPUSwapchainTextureFormat(device, window));
    }
    SDL_ReleaseGPUShader(device, show_vertex);
    SDL_ReleaseGPUShader(device, show_fragment);
    if (show_pipeline_own == nullptr || (window != nullptr && show_pipeline == nullptr)) {
        return sdl_error("the pipeline that shows");
    }
    return {};
}

SDL_GPUTexture* GpuRenderer::State::texture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int width,
                                            int height) {
    const SDL_GPUTextureCreateInfo made{.type = SDL_GPU_TEXTURETYPE_2D,
                                        .format = format,
                                        .usage = usage,
                                        .width = static_cast<Uint32>(width),
                                        .height = static_cast<Uint32>(height),
                                        .layer_count_or_depth = 1,
                                        .num_levels = 1,
                                        .sample_count = SDL_GPU_SAMPLECOUNT_1,
                                        .props = 0};
    return SDL_CreateGPUTexture(device, &made);
}

// A picture of palette indices, to draw into and to show from.
SDL_GPUTexture* GpuRenderer::State::picture_texture(int width, int height) {
    return texture(SDL_GPU_TEXTUREFORMAT_R8_UINT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                   std::max(width, kScreenWidth), height + kHudHeight);
}

// The buffers the quads go through hold at least as many as there are now.
std::expected<void, std::string> GpuRenderer::State::room_for_quads() {
    if (quads.size() <= quads_room) {
        return {};
    }
    SDL_ReleaseGPUBuffer(device, quads_buffer);
    SDL_ReleaseGPUTransferBuffer(device, quads_upload);
    quads_room = std::max<std::size_t>(2 * quads.size(), 4096);
    const auto size = static_cast<Uint32>(quads_room * sizeof(Quad));
    const SDL_GPUBufferCreateInfo buffer{.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = size, .props = 0};
    const SDL_GPUTransferBufferCreateInfo upload{.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size, .props = 0};
    quads_buffer = SDL_CreateGPUBuffer(device, &buffer);
    quads_upload = SDL_CreateGPUTransferBuffer(device, &upload);
    if (quads_buffer == nullptr || quads_upload == nullptr) {
        quads_room = 0;
        return sdl_error("the quads' buffer");
    }
    return {};
}

bool GpuRenderer::State::place_shared(const SharedGraphics& graphics) {
    bool fits = true;
    const auto all = [this, &fits](const std::vector<assets::IndexedImage>& images, std::vector<Spot>& spots) {
        spots.clear();
        for (const assets::IndexedImage& image : images) {
            const auto spot = atlas.put(image);
            fits = fits && spot.has_value();
            spots.push_back(spot.value_or(Spot{}));
        }
    };
    all(graphics.font, shared.font);
    all(graphics.portraits, shared.portraits);
    all(graphics.health, shared.health);
    all(graphics.items, shared.items);
    const auto panel = atlas.put(graphics.hud_panel);
    // The picker's corners, as a mask over its icon.
    constexpr int kIcon = 16;
    const auto mask = atlas.place(kIcon, kIcon);
    if (!panel || !mask) {
        return false;
    }
    for (int y = 0; y < kIcon; ++y) {
        for (int x = 0; x < kIcon; ++x) {
            *atlas.at(mask->x + x, mask->y + y) =
                picker_pixel(static_cast<std::size_t>(x), static_cast<std::size_t>(y)) ? 1 : 0;
        }
    }
    shared.panel = *panel;
    shared.picker_mask = *mask;
    shared.of = &graphics;
    return fits;
}

bool GpuRenderer::State::place_scene(const SceneGraphics& graphics) {
    // The tileset as rows of tiles, and under it the same rows of their masks: a tile's mask is the same way from
    // the tile everywhere.
    const int rows = (static_cast<int>(graphics.tileset.size()) + kTilesAcross - 1) / kTilesAcross;
    const auto tiles = atlas.place(kAtlasSize, rows * kTileSize);
    const auto masks = atlas.place(kAtlasSize, rows * kTileSize);
    if (!tiles || !masks) {
        return false;
    }
    for (std::size_t tile = 0; tile < graphics.tileset.size(); ++tile) {
        const assets::IndexedImage& image = graphics.tileset[tile];
        const int left = static_cast<int>(tile % kTilesAcross) * kTileSize;
        const int top = static_cast<int>(tile / kTilesAcross) * kTileSize;
        for (int y = 0; y < kTileSize; ++y) {
            for (int x = 0; x < kTileSize; ++x) {
                const auto sx = static_cast<std::size_t>(x);
                const auto sy = static_cast<std::size_t>(y);
                const bool has = sx < image.width && sy < image.height;
                *atlas.at(tiles->x + left + x, tiles->y + top + y) = has ? image.at(sx, sy) : std::uint8_t{0};
                *atlas.at(masks->x + left + x, masks->y + top + y) =
                    has && in_front(graphics.tile_masks, tile, sx, sy) ? 1 : 0;
            }
        }
    }
    scene.tiles = *tiles;
    scene.masks = *masks;
    for (const auto& [image, spot] :
         {std::pair{&graphics.picture, &scene.picture}, std::pair{&graphics.hud_picture, &scene.hud_picture}}) {
        if (*image) {
            const auto placed = atlas.put(**image);
            if (!placed) {
                return false;
            }
            *spot = *placed;
        }
    }
    scene.id = graphics.id;
    scene.placed = true;
    return true;
}

// The map's tiles in view, as draw_tiles of render.cpp.
void GpuRenderer::State::list_tiles(const View& view, const SceneGraphics& graphics, bool foreground) {
    if (view.map == nullptr || view.map->width == 0) {
        return;
    }
    const Map& map = *view.map;
    const int first_column = std::max(0, view.camera_x / kCellSize);
    const int first_row = std::max(0, view.camera_y / kCellSize);
    const int last_column = std::min(map.width - 1, (view.camera_x + view.width - 1) / kCellSize);
    const int last_row = std::min(map.height - 1, (view.camera_y + view.height - 1) / kCellSize);
    for (int row = first_row; row <= last_row; ++row) {
        for (int column = first_column; column <= last_column; ++column) {
            std::size_t metatile = map.at(column, row).metatile;
            if (metatile >= graphics.metatiles.size()) {
                metatile &= 0x1ffU;  // as the original's tables wrap
            }
            if (metatile >= graphics.metatiles.size()) {
                continue;
            }
            for (std::size_t part = 0; part < 4; ++part) {
                const assets::TileRef& ref = graphics.metatiles[metatile].parts.at(part);
                if (ref.tile >= graphics.tileset.size() || (foreground && !ref.foreground())) {
                    continue;
                }
                const Spot from{.x = scene.tiles.x + (static_cast<int>(ref.tile % kTilesAcross) * kTileSize),
                                .y = scene.tiles.y + (static_cast<int>(ref.tile / kTilesAcross) * kTileSize)};
                Quad tile = quad(from, (column * kCellSize) + (static_cast<int>(part % 2) * kTileSize) - view.camera_x,
                                 (row * kCellSize) + (static_cast<int>(part / 2) * kTileSize) - view.camera_y,
                                 kTileSize, kTileSize);
                tile.mirrored = ref.flip_h() ? 1 : 0;
                tile.flipped = ref.flip_v() ? 1 : 0;
                if (foreground) {
                    tile.mask_dx = static_cast<float>(scene.masks.x - scene.tiles.x);
                    tile.mask_dy = static_cast<float>(scene.masks.y - scene.tiles.y);
                    tile.masked = 1;
                }
                quads.push_back(tile);
            }
        }
    }
}

// As draw_hud of render.cpp.
void GpuRenderer::State::list_hud(const HudView& hud, const SharedGraphics& graphics, int top) {
    const auto whole = [this](const assets::IndexedImage& image, Spot from, int x, int y) {
        quads.push_back(quad(from, x, y, static_cast<int>(image.width), static_cast<int>(image.height)));
    };
    whole(graphics.hud_panel, shared.panel, 0, top);
    for (std::size_t who = 0; who < hud.vikings.size(); ++who) {
        const HudView::Viking& viking = hud.vikings[who];
        if (!viking.shown) {
            continue;
        }
        const std::size_t portrait = (static_cast<std::size_t>(viking.portrait) * 3) + who;
        if (portrait < graphics.portraits.size()) {
            whole(graphics.portraits[portrait], shared.portraits[portrait], kPortraitAt.at(who)[0],
                  top + kPortraitAt.at(who)[1]);
        }
        if (viking.health >= 0 && std::cmp_less(viking.health, graphics.health.size())) {
            const auto health = static_cast<std::size_t>(viking.health);
            whole(graphics.health[health], shared.health[health], kHealthAt.at(who)[0], top + kHealthAt.at(who)[1]);
        }
    }
    for (std::size_t square = 0; square < kSquares; ++square) {
        const int x = kSquareAt.at(square)[0];
        const int y = top + kSquareAt.at(square)[1];
        if (const std::optional<int> icon = hud.icons.at(square);
            icon && *icon >= 0 && std::cmp_less(*icon, graphics.items.size())) {
            const auto item = static_cast<std::size_t>(*icon);
            whole(graphics.items[item], shared.items[item], x, y);
        }
        if (hud.picker.at(square) && kPickerIcon < graphics.items.size()) {
            const assets::IndexedImage& frame = graphics.items[kPickerIcon];
            Quad picker =
                quad(shared.items[kPickerIcon], x, y, static_cast<int>(frame.width), static_cast<int>(frame.height));
            picker.mask_dx = static_cast<float>(shared.picker_mask.x - shared.items[kPickerIcon].x);
            picker.mask_dy = static_cast<float>(shared.picker_mask.y - shared.items[kPickerIcon].y);
            picker.masked = 1;
            quads.push_back(picker);
        }
    }
}

// Where each of the view's sprites is in the atlas, those not yet there put into it. False if one has no room.
bool GpuRenderer::State::place_pieces(const View& view) {
    piece_spots.assign(view.sprites.size(), Spot{});
    for (std::size_t i = 0; i < view.sprites.size(); ++i) {
        const assets::IndexedImage* image = view.sprites[i].image;
        if (image == nullptr) {
            continue;
        }
        // FNV-1a over the size and the pixels.
        constexpr std::uint64_t kPrime = 0x100000001b3ULL;
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        const auto mix = [&hash](std::uint64_t value) { hash = (hash ^ value) * kPrime; };
        mix(image->width);
        mix(image->height);
        for (const std::uint8_t pixel : image->pixels) {
            mix(pixel);
        }
        auto found = pieces.find(hash);
        if (found == pieces.end()) {
            const auto spot = atlas.put(*image);
            if (!spot) {
                return false;
            }
            found = pieces.emplace(hash, *spot).first;
        }
        piece_spots[i] = found->second;
    }
    return true;
}

// Everything the picture shows, in the order it is drawn: first what stays above the HUD (`in_playfield` of
// them), then what is drawn in the HUD's place. False when the atlas has no room for the sprites.
bool GpuRenderer::State::list(const View& view, const SceneGraphics& graphics, std::size_t& in_playfield) {
    constexpr std::uint8_t kFirstGlyph = 0x10;
    const SharedGraphics& common = *shared.of;
    quads.clear();
    if (graphics.picture) {
        quads.push_back(quad(scene.picture, -view.camera_x, kPictureTop - view.camera_y,
                             static_cast<int>(graphics.picture->width), static_cast<int>(graphics.picture->height)));
    }
    list_tiles(view, graphics, false);
    if (!place_pieces(view)) {  // the atlas is full of sprites no longer shown: without them, then
        atlas.back_to(after_scene);
        pieces.clear();
        if (!place_pieces(view)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < view.sprites.size(); ++i) {
        const DrawnSprite& sprite = view.sprites[i];
        if (sprite.image == nullptr) {
            continue;
        }
        Quad piece = quad(piece_spots[i], sprite.x - view.camera_x, sprite.y - view.camera_y,
                          static_cast<int>(sprite.image->width), static_cast<int>(sprite.image->height));
        piece.mirrored = sprite.mirrored ? 1 : 0;
        piece.flipped = sprite.flipped ? 1 : 0;
        piece.see_through = 1;
        quads.push_back(piece);
    }
    list_tiles(view, graphics, true);
    for (int row = 0; row < std::min(view.height / kTileSize, kTextRows); ++row) {
        for (int column = 0; column < std::min(view.width / kTileSize, kTextColumns); ++column) {
            const std::uint8_t character =
                view.text[(static_cast<std::size_t>(row) * kTextColumns) + static_cast<std::size_t>(column)];
            if (character < kFirstGlyph || std::size_t{character} - kFirstGlyph >= common.font.size()) {
                continue;
            }
            const std::size_t glyph = std::size_t{character} - kFirstGlyph;
            Quad letter = quad(shared.font[glyph], view.text_x + (column * kTileSize), view.text_y + (row * kTileSize),
                               static_cast<int>(common.font[glyph].width), static_cast<int>(common.font[glyph].height));
            letter.see_through = 1;
            quads.push_back(letter);
        }
    }
    in_playfield = quads.size();
    if (graphics.hud_picture) {  // its first 64 rows, in the HUD's place
        quads.push_back(quad(scene.hud_picture, 0, view.height,
                             std::min(kScreenWidth, static_cast<int>(graphics.hud_picture->width)),
                             std::min(kHudHeight, static_cast<int>(graphics.hud_picture->height))));
    }
    if (view.hud) {
        list_hud(*view.hud, common, view.height);
    }
    return true;
}

GpuRenderer::GpuRenderer(std::unique_ptr<State> state) : state_{std::move(state)} {}
GpuRenderer::~GpuRenderer() = default;

std::expected<std::unique_ptr<GpuRenderer>, std::string> GpuRenderer::open_headless() {
    auto state = std::make_unique<State>();
    // No display is asked for: the picture is drawn into a texture and read back.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return sdl_error("SDL's video");
    }
    state->video_started = true;
    state->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    if (state->device == nullptr) {
        return sdl_error("no graphics device");
    }
    if (auto ready = state->set_up(); !ready) {
        return std::unexpected{ready.error()};
    }
    return std::unique_ptr<GpuRenderer>{new GpuRenderer{std::move(state)}};
}

std::expected<std::unique_ptr<GpuRenderer>, std::string> GpuRenderer::open_for(SDL_Window* window) {
    auto state = std::make_unique<State>();
    state->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    if (state->device == nullptr) {
        return sdl_error("no graphics device");
    }
    if (!SDL_ClaimWindowForGPUDevice(state->device, window)) {
        return sdl_error("the window");
    }
    state->window = window;
    if (auto ready = state->set_up(); !ready) {
        return std::unexpected{ready.error()};
    }
    return std::unique_ptr<GpuRenderer>{new GpuRenderer{std::move(state)}};
}

// The view's picture drawn onto a texture of palette indices.
std::expected<void, std::string> GpuRenderer::State::render(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* onto,
                                                            const View& view, const SceneGraphics& graphics,
                                                            const SharedGraphics& common) {
    // The atlas: the shared pictures once, the scene's when they are others, the sprites' every time.
    if (shared.of != &common) {
        atlas.back_to({});
        if (!place_shared(common)) {
            return std::unexpected{std::string{"the atlas has no room for the shared pictures"}};
        }
        after_shared = atlas.mark();
        scene.placed = false;
    }
    if (!scene.placed || graphics.id == 0 || graphics.id != scene.id) {
        atlas.back_to(after_shared);
        scene = {};
        pieces.clear();
        if (!place_scene(graphics)) {
            return std::unexpected{std::string{"the atlas has no room for the scene's pictures"}};
        }
        after_scene = atlas.mark();
    }
    std::size_t in_playfield = 0;
    if (!list(view, graphics, in_playfield)) {
        return std::unexpected{std::string{"the atlas has no room for the sprites"}};
    }
    if (auto room = room_for_quads(); !room) {
        return room;
    }

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    if (const auto [top, bottom] = atlas.take_changed(); top < bottom) {
        const std::size_t bytes = static_cast<std::size_t>(bottom - top) * kAtlasSize;
        void* mapped = SDL_MapGPUTransferBuffer(device, atlas_upload, true);
        if (mapped == nullptr) {
            return sdl_error("the atlas");
        }
        std::memcpy(mapped, atlas.at(0, top), bytes);
        SDL_UnmapGPUTransferBuffer(device, atlas_upload);
        const SDL_GPUTextureTransferInfo from{
            .transfer_buffer = atlas_upload, .offset = 0, .pixels_per_row = kAtlasSize, .rows_per_layer = 0};
        const SDL_GPUTextureRegion to{.texture = atlas_texture,
                                      .mip_level = 0,
                                      .layer = 0,
                                      .x = 0,
                                      .y = static_cast<Uint32>(top),
                                      .z = 0,
                                      .w = kAtlasSize,
                                      .h = static_cast<Uint32>(bottom - top),
                                      .d = 1};
        SDL_UploadToGPUTexture(copy, &from, &to, false);
    }
    if (!quads.empty()) {
        const std::size_t bytes = quads.size() * sizeof(Quad);
        void* mapped = SDL_MapGPUTransferBuffer(device, quads_upload, true);
        if (mapped == nullptr) {
            return sdl_error("the quads");
        }
        std::memcpy(mapped, quads.data(), bytes);
        SDL_UnmapGPUTransferBuffer(device, quads_upload);
        const SDL_GPUTransferBufferLocation from{.transfer_buffer = quads_upload, .offset = 0};
        const SDL_GPUBufferRegion to{.buffer = quads_buffer, .offset = 0, .size = static_cast<Uint32>(bytes)};
        SDL_UploadToGPUBuffer(copy, &from, &to, true);
    }
    SDL_EndGPUCopyPass(copy);

    SDL_GPUColorTargetInfo colour{};
    colour.texture = onto;
    colour.load_op = SDL_GPU_LOADOP_CLEAR;
    colour.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &colour, 1, nullptr);
    if (!quads.empty()) {
        constexpr std::uint32_t kCorners = 6;
        const std::array<float, 2> size{static_cast<float>(std::max(view.width, kScreenWidth)),
                                        static_cast<float>(view.height + kHudHeight)};
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_PushGPUVertexUniformData(commands, 0, size.data(), sizeof(size));
        const SDL_GPUBufferBinding buffer{.buffer = quads_buffer, .offset = 0};
        SDL_BindGPUVertexBuffers(pass, 0, &buffer, 1);
        const SDL_GPUTextureSamplerBinding from_atlas{.texture = atlas_texture, .sampler = sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &from_atlas, 1);
        const SDL_Rect playfield{.x = 0, .y = 0, .w = view.width, .h = view.height};
        const SDL_Rect whole{.x = 0, .y = 0, .w = std::max(view.width, kScreenWidth), .h = view.height + kHudHeight};
        if (in_playfield != 0) {
            SDL_SetGPUScissor(pass, &playfield);
            SDL_DrawGPUPrimitives(pass, kCorners, static_cast<Uint32>(in_playfield), 0, 0);
        }
        if (in_playfield != quads.size()) {
            SDL_SetGPUScissor(pass, &whole);
            SDL_DrawGPUPrimitives(pass, kCorners, static_cast<Uint32>(quads.size() - in_playfield), 0,
                                  static_cast<Uint32>(in_playfield));
        }
    }
    SDL_EndGPURenderPass(pass);

    return {};
}

// A kept picture onto a target of `width` x `height`: as large as whole pixels allow, in the middle, the rest
// black.
std::expected<void, std::string> GpuRenderer::State::show(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* onto,
                                                          int width, int height, const Screen& screen,
                                                          SDL_GPUGraphicsPipeline* with) {
    const Kept picture = screen.kept < 0 ? Kept{} : kept.at(static_cast<std::size_t>(screen.kept) % kKept);
    void* mapped = SDL_MapGPUTransferBuffer(device, palette_upload, true);
    if (mapped == nullptr) {
        return sdl_error("the palette");
    }
    auto* colours = static_cast<std::uint8_t*>(mapped);
    for (std::size_t i = 0; i < kColours; ++i) {
        const assets::Rgb& colour = screen.palette.at(i);
        const std::array<std::uint8_t, 4> rgba{colour.r, colour.g, colour.b, 255};
        std::memcpy(colours + (i * 4), rgba.data(), rgba.size());  // NOLINT(*-pointer-arithmetic)
    }
    SDL_UnmapGPUTransferBuffer(device, palette_upload);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTextureTransferInfo from{
        .transfer_buffer = palette_upload, .offset = 0, .pixels_per_row = kColours, .rows_per_layer = 0};
    const SDL_GPUTextureRegion to{
        .texture = palette_texture, .mip_level = 0, .layer = 0, .x = 0, .y = 0, .z = 0, .w = kColours, .h = 1, .d = 1};
    SDL_UploadToGPUTexture(copy, &from, &to, true);
    SDL_EndGPUCopyPass(copy);

    const int scale = std::max(1, std::min(width / kScreenWidth, height / kScreenHeight));
    const int left = (width - (kScreenWidth * scale)) / 2;  // whole pixels
    const int top = (height - (kScreenHeight * scale)) / 2;
    SDL_GPUColorTargetInfo colour{};
    colour.texture = onto;
    colour.clear_color = {.r = 0, .g = 0, .b = 0, .a = 1};
    colour.load_op = SDL_GPU_LOADOP_CLEAR;
    colour.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &colour, 1, nullptr);
    if (picture.texture != nullptr) {  // none kept (yet): black
        constexpr std::uint32_t kCorners = 6;
        constexpr float kMostSamples = 4;
        struct Placed {  // gpu_show.vert
            std::array<float, 4> dest;
            std::array<float, 4> source;
            std::array<float, 2> size;
            std::array<float, 2> unused;
        };
        struct Sampled {  // gpu_show.frag
            std::array<float, 4> within;
            float samples;
            std::array<float, 3> unused;
        };
        SDL_BindGPUGraphicsPipeline(pass, with);
        const std::array<SDL_GPUTextureSamplerBinding, 2> pictures{
            {{.texture = picture.texture, .sampler = sampler}, {.texture = palette_texture, .sampler = sampler}}};
        SDL_BindGPUFragmentSamplers(pass, 0, pictures.data(), 2);
        const auto part = [&](std::array<float, 4> dest, std::array<float, 4> source, std::array<float, 4> within) {
            // Shown smaller than it is, a pixel is the mean of what it covers (up to 4 x 4 of the picture's).
            const float covers = source[2] / dest[2];
            const Placed placed{.dest = dest,
                                .source = source,
                                .size = {static_cast<float>(width), static_cast<float>(height)},
                                .unused = {}};
            const Sampled sampled{
                .within = within, .samples = std::clamp(std::ceil(covers - 0.01F), 1.0F, kMostSamples), .unused = {}};
            SDL_PushGPUVertexUniformData(commands, 0, &placed, sizeof(placed));
            SDL_PushGPUFragmentUniformData(commands, 0, &sampled, sizeof(sampled));
            SDL_DrawGPUPrimitives(pass, kCorners, 1, 0, 0);
        };
        // The playfield's part with the HUD under it, both at one scale: as large as the original's screen's
        // place holds them. The original's view fills it; a larger one makes everything smaller, the HUD too.
        const auto screen_width = static_cast<float>(kScreenWidth * scale);
        const auto screen_height = static_cast<float>(kScreenHeight * scale);
        const Screen::Part shown{.x = std::clamp(screen.part.x, 0.0F, static_cast<float>(picture.width)),
                                 .y = std::clamp(screen.part.y, 0.0F, static_cast<float>(picture.height)),
                                 .width = std::clamp(screen.part.width, 1.0F, static_cast<float>(picture.width)),
                                 .height = std::clamp(screen.part.height, 1.0F, static_cast<float>(picture.height))};
        // A view larger than the original's is as high as the whole screen, and the HUD lies over its bottom.
        const bool over = shown.height > kPlayfieldHeight;
        const float fit = std::min(screen_width / std::max(shown.width, static_cast<float>(kScreenWidth)),
                                   screen_height / (over ? std::max(shown.height, static_cast<float>(kScreenHeight))
                                                         : shown.height + kHudHeight));
        const float field_height = shown.height * fit;
        const float all_top =
            over ? static_cast<float>(top)
                 : static_cast<float>(top) + ((screen_height - (field_height + (kHudHeight * fit))) / 2);
        const auto middle = [&](float wide) { return static_cast<float>(left) + ((screen_width - wide) / 2); };
        part({middle(shown.width * fit), all_top, shown.width * fit, field_height},
             {shown.x, shown.y, shown.width, shown.height},
             {0, 0, static_cast<float>(picture.width), static_cast<float>(picture.height)});
        const auto hud_top = static_cast<float>(picture.height);
        const float hud_at =
            over ? static_cast<float>(top) + screen_height - (kHudHeight * fit) : all_top + field_height;
        part({middle(kScreenWidth * fit), hud_at, kScreenWidth * fit, kHudHeight * fit},
             {0, hud_top, kScreenWidth, kHudHeight}, {0, hud_top, kScreenWidth, hud_top + kHudHeight});
    }
    SDL_EndGPURenderPass(pass);
    return {};
}

std::expected<void, std::string> GpuRenderer::draw(Screen& screen, const View& view, const SceneGraphics& scene,
                                                   const SharedGraphics& shared) {
    State& s = *state_;
    screen.palette = darkened(view.palette, view.darken, view.darken_all);
    if (view.colour3) {
        screen.palette[3] = *view.colour3;
    }
    if (view.width != kScreenWidth || view.height != kPlayfieldHeight) {
        return std::unexpected{std::string{"only the original's view can be read back"}};
    }
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(s.device);
    if (commands == nullptr) {
        return sdl_error("no command buffer");
    }
    if (auto drawn = s.render(commands, s.target, view, scene, shared); !drawn) {
        SDL_CancelGPUCommandBuffer(commands);
        return drawn;
    }
    SDL_GPUCopyPass* copy = nullptr;
    copy = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTextureRegion from{.texture = s.target,
                                    .mip_level = 0,
                                    .layer = 0,
                                    .x = 0,
                                    .y = 0,
                                    .z = 0,
                                    .w = kScreenWidth,
                                    .h = kScreenHeight,
                                    .d = 1};
    const SDL_GPUTextureTransferInfo to{
        .transfer_buffer = s.download, .offset = 0, .pixels_per_row = kScreenWidth, .rows_per_layer = 0};
    SDL_DownloadFromGPUTexture(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* done = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (done == nullptr) {
        return sdl_error("the picture");
    }
    const bool waited = SDL_WaitForGPUFences(s.device, true, &done, 1);
    SDL_ReleaseGPUFence(s.device, done);
    const void* mapped = waited ? SDL_MapGPUTransferBuffer(s.device, s.download, false) : nullptr;
    if (mapped == nullptr) {
        return sdl_error("the picture read back");
    }
    std::memcpy(screen.pixels.data(), mapped, screen.pixels.size());
    SDL_UnmapGPUTransferBuffer(s.device, s.download);
    return {};
}

std::expected<void, std::string> GpuRenderer::keep(Screen& screen, const View& view, const SceneGraphics& scene,
                                                   const SharedGraphics& shared) {
    State& s = *state_;
    screen.palette = darkened(view.palette, view.darken, view.darken_all);
    if (view.colour3) {
        screen.palette[3] = *view.colour3;
    }
    screen.pixels.clear();
    State::Kept& picture = s.kept.at(s.kept_count % kKept);
    if (picture.texture == nullptr || picture.width != view.width || picture.height != view.height) {
        SDL_ReleaseGPUTexture(s.device, picture.texture);
        picture = {.texture = s.picture_texture(view.width, view.height), .width = view.width, .height = view.height};
        if (picture.texture == nullptr) {
            return sdl_error("a kept picture");
        }
    }
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(s.device);
    if (commands == nullptr) {
        return sdl_error("no command buffer");
    }
    if (auto drawn = s.render(commands, picture.texture, view, scene, shared); !drawn) {
        SDL_CancelGPUCommandBuffer(commands);
        return drawn;
    }
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return sdl_error("the picture");
    }
    screen.kept = static_cast<int>(s.kept_count++ % (kKept * 1024));
    return {};
}

std::expected<void, std::string> GpuRenderer::show(const Screen& screen) {
    State& s = *state_;
    if (s.window == nullptr) {
        return std::unexpected{std::string{"no window"}};
    }
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(s.device);
    if (commands == nullptr) {
        return sdl_error("no command buffer");
    }
    SDL_GPUTexture* onto = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, s.window, &onto, &width, &height)) {
        SDL_CancelGPUCommandBuffer(commands);
        return sdl_error("the window's picture");
    }
    if (onto != nullptr) {  // none: the window is hidden; nothing to show on
        if (auto shown =
                s.show(commands, onto, static_cast<int>(width), static_cast<int>(height), screen, s.show_pipeline);
            !shown) {
            (void)SDL_SubmitGPUCommandBuffer(commands);
            return shown;
        }
    }
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return sdl_error("the window's picture");
    }
    return {};
}

std::expected<std::vector<std::uint8_t>, std::string> GpuRenderer::shown(const Screen& screen, int scale) {
    State& s = *state_;
    const int width = kScreenWidth * scale;
    const int height = kScreenHeight * scale;
    const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    SDL_GPUTexture* onto =
        s.texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, width, height);
    const SDL_GPUTransferBufferCreateInfo made{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = static_cast<Uint32>(bytes), .props = 0};
    SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(s.device, &made);
    const auto read_back = [&]() -> std::expected<std::vector<std::uint8_t>, std::string> {
        SDL_GPUCommandBuffer* commands =
            onto != nullptr && download != nullptr ? SDL_AcquireGPUCommandBuffer(s.device) : nullptr;
        if (commands == nullptr) {
            return sdl_error("no command buffer");
        }
        if (auto drawn = s.show(commands, onto, width, height, screen, s.show_pipeline_own); !drawn) {
            SDL_CancelGPUCommandBuffer(commands);
            return std::unexpected{drawn.error()};
        }
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        const SDL_GPUTextureRegion from{.texture = onto,
                                        .mip_level = 0,
                                        .layer = 0,
                                        .x = 0,
                                        .y = 0,
                                        .z = 0,
                                        .w = static_cast<Uint32>(width),
                                        .h = static_cast<Uint32>(height),
                                        .d = 1};
        const SDL_GPUTextureTransferInfo to{.transfer_buffer = download,
                                            .offset = 0,
                                            .pixels_per_row = static_cast<Uint32>(width),
                                            .rows_per_layer = 0};
        SDL_DownloadFromGPUTexture(copy, &from, &to);
        SDL_EndGPUCopyPass(copy);
        SDL_GPUFence* done = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        if (done == nullptr) {
            return sdl_error("the picture shown");
        }
        const bool waited = SDL_WaitForGPUFences(s.device, true, &done, 1);
        SDL_ReleaseGPUFence(s.device, done);
        const void* mapped = waited ? SDL_MapGPUTransferBuffer(s.device, download, false) : nullptr;
        if (mapped == nullptr) {
            return sdl_error("the picture shown, read back");
        }
        std::vector<std::uint8_t> pixels(bytes);
        std::memcpy(pixels.data(), mapped, bytes);
        SDL_UnmapGPUTransferBuffer(s.device, download);
        return pixels;
    };
    auto pixels = read_back();
    SDL_ReleaseGPUTransferBuffer(s.device, download);
    SDL_ReleaseGPUTexture(s.device, onto);
    return pixels;
}

}  // namespace lvr
