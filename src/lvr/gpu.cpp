#include "lvr/gpu.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lvr/backdrop.hpp"

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
constexpr std::uint32_t kOverFragmentShader[] =
#include "gpu_over.frag.inc"
    ;
constexpr std::uint32_t kMarkFragmentShader[] =
#include "gpu_mark.frag.inc"
    ;
constexpr std::uint32_t kGaugeFragmentShader[] =
#include "gpu_gauge.frag.inc"
    ;
constexpr std::uint32_t kPanelFragmentShader[] =
#include "gpu_panel.frag.inc"
    ;
constexpr std::uint32_t kXbrFragmentShader[] =
#include "gpu_xbr.frag.inc"
    ;
constexpr std::uint32_t kHyllianFragmentShader[] =
#include "gpu_hyllian.frag.inc"
    ;

// How many pictures are kept for showing: more than two steps make (a change of scene is some 150), since the
// last of a step is still shown while the next step's are drawn.
constexpr std::size_t kKept = 512;
// Atlas pages: as many as kept pictures need at once (two at a change of scene); past this many, the pictures
// on the one used longest ago are let go.
constexpr std::size_t kMostPages = 4;
// A kept picture is drawn when it is shown, this many pixels a pixel of it at most (as many as the window shows
// it at: what moves between ticks can stop between the original's pixels).
constexpr int kMostScale = 12;
// What a quad of a kept picture moves with between ticks (else: the view's sprite it is).
constexpr std::int32_t kWithCamera = -1;  // the map, the scene's picture, the text
constexpr std::int32_t kStays = -2;       // the HUD
constexpr std::size_t kColours = 256;
constexpr int kHudHeight = kScreenHeight - kPlayfieldHeight;
// The minimap as it is shown: a square as high as the HUD, of this many of the map's cells a side. The HUD makes
// room by a column of its stone tiles: the one at its left end is under the minimap then, the one at its right end
// off the screen. It slides there over this many pictures before the minimap is put in, and back after it is taken
// away.
constexpr int kMinimapSide = kHudHeight;
constexpr int kMinimapCells = 64;
constexpr int kHudSpare = 31;  // (a pixel less than a tile: the panel's tiles start a pixel in)
// (In refreshes of the original's display, by the time gone: however many pictures a second are shown.)
constexpr float kSlideRefreshes = 10;
constexpr float kFadeRefreshes = 7;
constexpr float kMarkRefreshes = 18;         // the rewind's mark: shown this long, then away this long
constexpr float kMostRefreshesAPicture = 6;  // (a picture shown late moves what slides no more than this)
// The Modern visuals' light (gpu_show.frag): how far around a colour that gives light its glow reaches, in the
// picture's pixels.
constexpr float kGlowReach = 5;
// The rewind's bar (Remaster rules), in the original's pixels from the minimap's corner: along its bottom, inside its
// frame, with a frame as wide as a pixel and corners of this radius (two pixels cut off at each: one, then the next
// row's first).
constexpr std::array<float, 4> kGaugeAt{3, 54, 58, 7};  // left, top, width, height
constexpr float kGaugeRadius = 3.5F;

constexpr int kAtlasSize = 2048;
constexpr int kTilesAcross = kAtlasSize / kTileSize;  // the tileset's tiles in the atlas, a row of them

struct Spot {
    int x = 0;
    int y = 0;

    bool operator==(const Spot&) const = default;
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
    SDL_GPUSampler* smooth = nullptr;  // for what is shown at a size not its own (the minimap)
    SDL_GPUTexture* target = nullptr;  // a picture drawn to be read back: the original's size
    SDL_GPUTexture* work = nullptr;    // a kept picture drawn to be shown: as large as it is shown
    int work_width = 0;
    int work_height = 0;
    SDL_GPUBuffer* quads_buffer = nullptr;
    SDL_GPUTransferBuffer* atlas_upload = nullptr;
    SDL_GPUTransferBuffer* quads_upload = nullptr;
    SDL_GPUTransferBuffer* download = nullptr;
    // Showing a kept picture: its colours looked up, onto the window or (for a test) a target read back.
    SDL_Window* window = nullptr;
    SDL_GPUGraphicsPipeline* show_pipeline = nullptr;      // onto the window
    SDL_GPUGraphicsPipeline* show_pipeline_own = nullptr;  // onto `shown_target`
    SDL_GPUTexture* palette_texture = nullptr;
    // A picture laid over what is shown (the minimap): blended by its alpha.
    SDL_GPUGraphicsPipeline* over_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* over_pipeline_own = nullptr;
    SDL_GPUTexture* over_texture = nullptr;
    SDL_GPUTransferBuffer* over_upload = nullptr;
    int over_width = 0;
    int over_height = 0;
    // The mark of time running backwards: two triangles, flashing.
    SDL_GPUGraphicsPipeline* mark_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* mark_pipeline_own = nullptr;
    // The rewind's bar.
    SDL_GPUGraphicsPipeline* gauge_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* gauge_pipeline_own = nullptr;
    // A picture laid over the screen (the menu, set_overlay): its pixels, whether the texture has them yet, and the
    // texture.
    SDL_GPUGraphicsPipeline* panel_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* panel_pipeline_own = nullptr;
    std::vector<std::uint8_t> overlay;
    bool mailbox = false;  // pictures shown without waiting for the refresh (the newest taken at each)
    bool overlay_sent = false;
    SDL_GPUTexture* overlay_texture = nullptr;
    SDL_GPUTransferBuffer* overlay_upload = nullptr;
    bool upload_overlay(SDL_GPUCommandBuffer* commands);
    // The screen's look (Look): what is shown is drawn into `crt_source` (the target's size), and from there onto
    // the target as the tube would show it.
    Look look = Look::kNone;
    SDL_GPUGraphicsPipeline* xbr_pipeline = nullptr;  // the xbr look's last pass
    SDL_GPUGraphicsPipeline* xbr_pipeline_own = nullptr;
    SDL_GPUGraphicsPipeline* hyllian_pipeline = nullptr;  // the crt look's
    SDL_GPUGraphicsPipeline* hyllian_pipeline_own = nullptr;
    SDL_GPUTexture* crt_source = nullptr;
    int crt_width = 0;
    int crt_height = 0;
    float marked = 0;                // for how many refreshes in a row it has been wanted
    std::uint64_t last_show_ns = 0;  // when the window last showed a picture (what slides and fades goes by it)
    float made_room = 0;             // how far the HUD has slid aside for the minimap: 0 not at all, 1 all the way
    float faded_in = 0;              // how much of the minimap is there: 0 nothing, 1 all of it
    SDL_GPUTransferBuffer* palette_upload = nullptr;
    // What stops the Modern visuals' light (Screen::ground): a texture of it, and which one it holds (0: the empty
    // one it starts with).
    SDL_GPUTexture* ground_texture = nullptr;
    std::uint64_t ground_id = 0;
    int ground_width = 0;
    int ground_height = 0;
    bool upload_ground(SDL_GPUCommandBuffer* commands, const Ground& ground);
    std::size_t quads_room = 0;  // how many the buffers hold
    bool video_started = false;

    // A page of the atlas: what it holds and where, on the CPU and on the card. The shared pictures and a
    // scene's are put in first; the sprites' pictures each the first time one is shown, found again by what it
    // looks like (a hash of its size and pixels). A kept picture is drawn from one page; a page is used for
    // another scene, or anew when its sprites fill it, only when no kept picture is drawn from it.
    struct Page {
        Atlas atlas;
        SDL_GPUTexture* texture = nullptr;
        const SharedGraphics* shared_of = nullptr;
        std::vector<Spot> font, portraits, health, items;
        Spot panel, picker_mask;
        std::uint32_t scene_id = 0;  // 0: none placed
        Spot tiles, masks, picture, hud_picture;
        std::optional<Spot> backdrop;  // the scene's, if it has one
        Atlas::Mark after_scene;
        std::unordered_map<std::uint64_t, Spot> pieces;
    };
    std::vector<std::unique_ptr<Page>> pages;
    std::size_t page = 0;  // the one pictures are kept on now
    std::size_t pages_set_up = 0;
    ShowTimes times;
    ShowTimes since;  // what was kept since the last show: moved into `times` by it
    // A kept picture: the quads it is drawn with, made when it was kept and drawn when it is shown. The playfield
    // is `width` x `height` (the view's) with the HUD's rows under it.
    struct Picture {
        std::size_t page = 0;
        std::vector<Quad> quads;
        std::vector<std::int32_t> moves_with;  // a quad's: kWithCamera, kStays, or the view's sprite it is
        std::size_t in_playfield = 0;          // the first quads: those cut at the HUD
        int width = 0;
        int height = 0;
        // The scene's backdrop (backdrop.hpp), laid in where the map shows it when the picture is shown: where it
        // is in the atlas, how it moves, and where the map is on the picture (left, top, right, bottom; as kept,
        // before what moves between ticks).
        struct Behind {
            Spot at;
            int width = 0;
            int height = 0;
            std::uint16_t scroll_x = 0;
            std::uint16_t scroll_y = 0;
            std::array<int, 4> map{};

            bool operator==(const Behind&) const = default;
        };
        std::optional<Behind> backdrop;
    };
    // The kept pictures by number (the last kKept of them); one the same as the one before (a fade's: only the
    // colours change) is kept once.
    struct Kept {
        std::int64_t number = -1;
        std::shared_ptr<const Picture> picture;
    };
    std::vector<Kept> kept = std::vector<Kept>(kKept);
    std::int64_t kept_count = 0;   // how many were kept so far
    std::int64_t shown_kept = -1;  // the kept picture shown last in a window
    // What `work` holds when it is a picture that doesn't move between ticks: shown again, it isn't drawn anew.
    std::shared_ptr<const Picture> in_work;
    int in_work_scale = 0;
    Look in_work_look = Look::kNone;  // drawn for it
    std::vector<Spot> piece_spots;    // this picture's sprites, in order
    std::vector<Quad> quads;          // what is drawn, as it goes to the card

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        if (device != nullptr) {
            for (const auto& each : pages) {
                SDL_ReleaseGPUTexture(device, each->texture);
            }
            SDL_ReleaseGPUTexture(device, work);
            SDL_ReleaseGPUTransferBuffer(device, palette_upload);
            SDL_ReleaseGPUTransferBuffer(device, over_upload);
            SDL_ReleaseGPUTexture(device, over_texture);
            SDL_ReleaseGPUGraphicsPipeline(device, over_pipeline_own);
            SDL_ReleaseGPUGraphicsPipeline(device, mark_pipeline_own);
            SDL_ReleaseGPUGraphicsPipeline(device, mark_pipeline);
            SDL_ReleaseGPUGraphicsPipeline(device, gauge_pipeline_own);
            SDL_ReleaseGPUGraphicsPipeline(device, gauge_pipeline);
            SDL_ReleaseGPUGraphicsPipeline(device, panel_pipeline_own);
            SDL_ReleaseGPUGraphicsPipeline(device, panel_pipeline);
            SDL_ReleaseGPUTransferBuffer(device, overlay_upload);
            SDL_ReleaseGPUTexture(device, overlay_texture);
            SDL_ReleaseGPUGraphicsPipeline(device, xbr_pipeline_own);
            SDL_ReleaseGPUGraphicsPipeline(device, xbr_pipeline);
            SDL_ReleaseGPUGraphicsPipeline(device, hyllian_pipeline_own);
            SDL_ReleaseGPUGraphicsPipeline(device, hyllian_pipeline);
            SDL_ReleaseGPUTexture(device, crt_source);
            SDL_ReleaseGPUGraphicsPipeline(device, over_pipeline);
            SDL_ReleaseGPUTexture(device, palette_texture);
            SDL_ReleaseGPUTexture(device, ground_texture);
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
            SDL_ReleaseGPUSampler(device, sampler);
            SDL_ReleaseGPUSampler(device, smooth);
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
            SDL_DestroyGPUDevice(device);
        }
        if (video_started) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    std::expected<void, std::string> set_up();
    // xbr's and crt's last pass (`which`): from the picture's pixels on its grid.
    void show_xbr(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* onto, int width, int height,
                  const std::array<float, 4>& screen, const std::array<float, 4>& grid, bool own, Look which);
    SDL_GPUTexture* texture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int width, int height);
    SDL_GPUTexture* picture_texture(int width, int height);
    std::expected<Picture, std::string> make(SDL_GPUCommandBuffer* commands, const View& view,
                                             const SceneGraphics& graphics, const SharedGraphics& common);
    std::expected<void, std::string> render(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* onto,
                                            const Picture& picture, int scale, const Motion* motion, float between);
    // `whole` > 0: the view alone (without the rows the HUD lies over, or the HUD), this many pixels a pixel of it,
    // onto a target as large as that: a picture of a whole level.
    std::expected<void, std::string> show(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* onto, int width, int height,
                                          const Screen& screen, const Minimap* minimap, bool own, float between,
                                          int whole = 0);
    // A kept picture shown onto a target of `width` x `height` of our own, read back.
    std::expected<std::vector<std::uint8_t>, std::string> read_back(const Screen& screen, int width, int height,
                                                                    const Minimap* minimap, float between, int whole);
    bool upload_over(SDL_GPUCommandBuffer* commands, const Minimap& minimap);
    std::expected<Page*, std::string> free_page();
    std::expected<Page*, std::string> set_up_page(const SceneGraphics& graphics, const SharedGraphics& common);
    static bool place_shared(Page& on, const SharedGraphics& graphics);
    static bool place_scene(Page& on, const SceneGraphics& graphics);
    bool place_pieces(Page& on, const View& view);
    bool list(Page& on, const View& view, const SceneGraphics& graphics, Picture& picture);
    static void list_tiles(const Page& on, const View& view, const SceneGraphics& graphics, bool foreground,
                           Picture& picture);
    static void list_hud(const Page& on, const HudView& hud, const SharedGraphics& graphics, int top, Picture& picture);
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
    SDL_GPUSamplerCreateInfo linear = nearest;
    linear.min_filter = linear.mag_filter = SDL_GPU_FILTER_LINEAR;
    smooth = SDL_CreateGPUSampler(device, &linear);

    target = picture_texture(kScreenWidth, kPlayfieldHeight);
    palette_texture = texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, kColours, 1);
    const auto transfer = [this](SDL_GPUTransferBufferUsage usage, std::size_t size) {
        const SDL_GPUTransferBufferCreateInfo made{.usage = usage, .size = static_cast<Uint32>(size), .props = 0};
        return SDL_CreateGPUTransferBuffer(device, &made);
    };
    atlas_upload = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, std::size_t{kAtlasSize} * kAtlasSize);
    download = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, std::size_t{kScreenWidth} * kScreenHeight);
    palette_upload = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, kColours * 4);
    ground_texture = texture(SDL_GPU_TEXTUREFORMAT_R8_UINT, SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, 1);
    if (sampler == nullptr || smooth == nullptr || target == nullptr || atlas_upload == nullptr ||
        download == nullptr || palette_texture == nullptr || palette_upload == nullptr || ground_texture == nullptr) {
        return sdl_error("the textures");
    }

    // Showing: the same two shaders onto the window's format and onto our own.
    SDL_GPUShader* show_vertex = shader(kShowVertexShader, sizeof(kShowVertexShader), SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* show_fragment =
        shader(kShowFragmentShader, sizeof(kShowFragmentShader), SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 1);
    if (show_vertex == nullptr || show_fragment == nullptr) {
        return sdl_error("the shaders");
    }
    SDL_GPUShader* over_fragment =
        shader(kOverFragmentShader, sizeof(kOverFragmentShader), SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    SDL_GPUShader* mark_fragment =
        shader(kMarkFragmentShader, sizeof(kMarkFragmentShader), SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    SDL_GPUShader* gauge_fragment =
        shader(kGaugeFragmentShader, sizeof(kGaugeFragmentShader), SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    SDL_GPUShader* panel_fragment =
        shader(kPanelFragmentShader, sizeof(kPanelFragmentShader), SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    SDL_GPUShader* xbr_fragment =
        shader(kXbrFragmentShader, sizeof(kXbrFragmentShader), SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    SDL_GPUShader* hyllian_fragment =
        shader(kHyllianFragmentShader, sizeof(kHyllianFragmentShader), SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    if (over_fragment == nullptr || mark_fragment == nullptr || gauge_fragment == nullptr || xbr_fragment == nullptr ||
        panel_fragment == nullptr || hyllian_fragment == nullptr) {
        return sdl_error("the shaders");
    }
    const auto show_onto = [this, show_vertex](SDL_GPUTextureFormat format, SDL_GPUShader* fragment, bool blended) {
        SDL_GPUColorTargetDescription shown{};
        shown.format = format;
        if (blended) {
            shown.blend_state.enable_blend = true;
            shown.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            shown.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            shown.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            shown.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            shown.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            shown.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        SDL_GPUGraphicsPipelineCreateInfo made{};
        made.vertex_shader = show_vertex;
        made.fragment_shader = fragment;
        made.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        made.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        made.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        made.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        made.target_info.color_target_descriptions = &shown;
        made.target_info.num_color_targets = 1;
        return SDL_CreateGPUGraphicsPipeline(device, &made);
    };
    show_pipeline_own = show_onto(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, show_fragment, false);
    over_pipeline_own = show_onto(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, over_fragment, true);
    mark_pipeline_own = show_onto(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, mark_fragment, true);
    gauge_pipeline_own = show_onto(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, gauge_fragment, true);
    panel_pipeline_own = show_onto(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, panel_fragment, true);
    xbr_pipeline_own = show_onto(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, xbr_fragment, false);
    hyllian_pipeline_own = show_onto(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, hyllian_fragment, false);
    if (window != nullptr) {
        show_pipeline = show_onto(SDL_GetGPUSwapchainTextureFormat(device, window), show_fragment, false);
        over_pipeline = show_onto(SDL_GetGPUSwapchainTextureFormat(device, window), over_fragment, true);
        mark_pipeline = show_onto(SDL_GetGPUSwapchainTextureFormat(device, window), mark_fragment, true);
        gauge_pipeline = show_onto(SDL_GetGPUSwapchainTextureFormat(device, window), gauge_fragment, true);
        panel_pipeline = show_onto(SDL_GetGPUSwapchainTextureFormat(device, window), panel_fragment, true);
        xbr_pipeline = show_onto(SDL_GetGPUSwapchainTextureFormat(device, window), xbr_fragment, false);
        hyllian_pipeline = show_onto(SDL_GetGPUSwapchainTextureFormat(device, window), hyllian_fragment, false);
    }
    SDL_ReleaseGPUShader(device, show_vertex);
    SDL_ReleaseGPUShader(device, show_fragment);
    SDL_ReleaseGPUShader(device, over_fragment);
    SDL_ReleaseGPUShader(device, mark_fragment);
    SDL_ReleaseGPUShader(device, gauge_fragment);
    SDL_ReleaseGPUShader(device, panel_fragment);
    SDL_ReleaseGPUShader(device, xbr_fragment);
    SDL_ReleaseGPUShader(device, hyllian_fragment);
    if (show_pipeline_own == nullptr || over_pipeline_own == nullptr || mark_pipeline_own == nullptr ||
        gauge_pipeline_own == nullptr || xbr_pipeline_own == nullptr || panel_pipeline_own == nullptr ||
        hyllian_pipeline_own == nullptr ||
        (window != nullptr && (show_pipeline == nullptr || over_pipeline == nullptr || mark_pipeline == nullptr ||
                               gauge_pipeline == nullptr || xbr_pipeline == nullptr || panel_pipeline == nullptr ||
                               hyllian_pipeline == nullptr))) {
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

bool GpuRenderer::State::place_shared(Page& on, const SharedGraphics& graphics) {
    bool fits = true;
    const auto all = [&on, &fits](const std::vector<assets::IndexedImage>& images, std::vector<Spot>& spots) {
        spots.clear();
        for (const assets::IndexedImage& image : images) {
            const auto spot = on.atlas.put(image);
            fits = fits && spot.has_value();
            spots.push_back(spot.value_or(Spot{}));
        }
    };
    all(graphics.font, on.font);
    all(graphics.portraits, on.portraits);
    all(graphics.health, on.health);
    all(graphics.items, on.items);
    const auto panel = on.atlas.put(graphics.hud_panel);
    // The picker's corners, as a mask over its icon.
    constexpr int kIcon = 16;
    const auto mask = on.atlas.place(kIcon, kIcon);
    if (!panel || !mask) {
        return false;
    }
    for (int y = 0; y < kIcon; ++y) {
        for (int x = 0; x < kIcon; ++x) {
            *on.atlas.at(mask->x + x, mask->y + y) =
                picker_pixel(static_cast<std::size_t>(x), static_cast<std::size_t>(y)) ? 1 : 0;
        }
    }
    on.panel = *panel;
    on.picker_mask = *mask;
    on.shared_of = &graphics;
    return fits;
}

bool GpuRenderer::State::place_scene(Page& on, const SceneGraphics& graphics) {
    // The tileset as rows of tiles, and under it the same rows of their masks: a tile's mask is the same way from
    // the tile everywhere.
    const int rows = (static_cast<int>(graphics.tileset.size()) + kTilesAcross - 1) / kTilesAcross;
    const auto tiles = on.atlas.place(kAtlasSize, rows * kTileSize);
    const auto masks = on.atlas.place(kAtlasSize, rows * kTileSize);
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
                *on.atlas.at(tiles->x + left + x, tiles->y + top + y) = has ? image.at(sx, sy) : std::uint8_t{0};
                *on.atlas.at(masks->x + left + x, masks->y + top + y) =
                    has && in_front(graphics.tile_masks, tile, sx, sy) ? 1 : 0;
            }
        }
    }
    on.tiles = *tiles;
    on.masks = *masks;
    for (const auto& [image, spot] :
         {std::pair{&graphics.picture, &on.picture}, std::pair{&graphics.hud_picture, &on.hud_picture}}) {
        if (*image) {
            const auto placed = on.atlas.put(**image);
            if (!placed) {
                return false;
            }
            *spot = *placed;
        }
    }
    on.backdrop.reset();
    if (graphics.backdrop) {
        on.backdrop = on.atlas.put(graphics.backdrop->image);
        if (!on.backdrop) {
            return false;
        }
    }
    on.scene_id = graphics.id;
    return true;
}

// A page no kept picture is drawn from, or a new one. With as many as there may be, the pictures drawn from the
// one used longest ago are let go (they would show black).
std::expected<GpuRenderer::State::Page*, std::string> GpuRenderer::State::free_page() {
    std::vector<std::int64_t> newest(pages.size(), -1);  // the newest picture drawn from each page; -1: none
    for (const Kept& each : kept) {
        if (each.picture != nullptr && each.picture->page < pages.size()) {
            newest[each.picture->page] = std::max(newest[each.picture->page], each.number);
        }
    }
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (newest[i] < 0) {
            page = i;
            return pages[i].get();
        }
    }
    if (pages.size() < kMostPages) {
        auto made = std::make_unique<Page>();
        made->texture = texture(SDL_GPU_TEXTUREFORMAT_R8_UINT, SDL_GPU_TEXTUREUSAGE_SAMPLER, kAtlasSize, kAtlasSize);
        if (made->texture == nullptr) {
            return sdl_error("an atlas page");
        }
        pages.push_back(std::move(made));
        page = pages.size() - 1;
        return pages.back().get();
    }
    page = static_cast<std::size_t>(std::ranges::min_element(newest) - newest.begin());
    for (Kept& each : kept) {
        if (each.picture != nullptr && each.picture->page == page) {
            each = {};
        }
    }
    return pages[page].get();
}

// The page this view's picture is kept on: the one in use if it holds these pictures, else a free one with them
// put in.
std::expected<GpuRenderer::State::Page*, std::string> GpuRenderer::State::set_up_page(const SceneGraphics& graphics,
                                                                                      const SharedGraphics& common) {
    auto found = free_page();
    if (!found) {
        return found;
    }
    Page& on = **found;
    ++pages_set_up;
    on.atlas.back_to({});
    on.pieces.clear();
    on.scene_id = 0;
    on.shared_of = nullptr;
    if (!place_shared(on, common)) {
        return std::unexpected{std::string{"the atlas has no room for the shared pictures"}};
    }
    if (!place_scene(on, graphics)) {
        return std::unexpected{std::string{"the atlas has no room for the scene's pictures"}};
    }
    on.after_scene = on.atlas.mark();
    return &on;
}

// The map's tiles in view, as draw_tiles of render.cpp, and a cell more on each side (what moves between ticks
// shows a little past the view's edges; drawn as it is, that is cut away).
void GpuRenderer::State::list_tiles(const Page& on, const View& view, const SceneGraphics& graphics, bool foreground,
                                    Picture& picture) {
    if (view.map == nullptr || view.map->width == 0) {
        return;
    }
    const Map& map = *view.map;
    const int first_column = std::max(0, (view.camera_x / kCellSize) - 1);
    const int first_row = std::max(0, (view.camera_y / kCellSize) - 1);
    const int last_column = std::min(map.width - 1, ((view.camera_x + view.width - 1) / kCellSize) + 1);
    const int last_row = std::min(map.height - 1, ((view.camera_y + view.height - 1) / kCellSize) + 1);
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
                const Spot from{.x = on.tiles.x + (static_cast<int>(ref.tile % kTilesAcross) * kTileSize),
                                .y = on.tiles.y + (static_cast<int>(ref.tile / kTilesAcross) * kTileSize)};
                Quad tile = quad(from, (column * kCellSize) + (static_cast<int>(part % 2) * kTileSize) - view.camera_x,
                                 (row * kCellSize) + (static_cast<int>(part / 2) * kTileSize) - view.camera_y,
                                 kTileSize, kTileSize);
                tile.mirrored = ref.flip_h() ? 1 : 0;
                tile.flipped = ref.flip_v() ? 1 : 0;
                if (foreground) {
                    tile.mask_dx = static_cast<float>(on.masks.x - on.tiles.x);
                    tile.mask_dy = static_cast<float>(on.masks.y - on.tiles.y);
                    tile.masked = 1;
                }
                picture.quads.push_back(tile);
                picture.moves_with.push_back(kWithCamera);
            }
        }
    }
}

// As draw_hud of render.cpp.
void GpuRenderer::State::list_hud(const Page& on, const HudView& hud, const SharedGraphics& graphics, int top,
                                  Picture& picture) {
    const auto add = [&picture](const Quad& quad) {
        picture.quads.push_back(quad);
        picture.moves_with.push_back(kStays);
    };
    const auto whole = [&add](const assets::IndexedImage& image, Spot from, int x, int y) {
        add(quad(from, x, y, static_cast<int>(image.width), static_cast<int>(image.height)));
    };
    whole(graphics.hud_panel, on.panel, 0, top);
    for (std::size_t who = 0; who < hud.vikings.size(); ++who) {
        const HudView::Viking& viking = hud.vikings[who];
        if (!viking.shown) {
            continue;
        }
        const std::size_t portrait = (static_cast<std::size_t>(viking.portrait) * 3) + who;
        if (portrait < graphics.portraits.size()) {
            whole(graphics.portraits[portrait], on.portraits[portrait], kPortraitAt.at(who)[0],
                  top + kPortraitAt.at(who)[1]);
        }
        if (viking.health >= 0 && std::cmp_less(viking.health, graphics.health.size())) {
            const auto health = static_cast<std::size_t>(viking.health);
            whole(graphics.health[health], on.health[health], kHealthAt.at(who)[0], top + kHealthAt.at(who)[1]);
        }
    }
    // The letters of a name, not their cells, in the middle of a portrait's column; grey for one watching.
    const auto name_at = [&](const std::string& name, std::size_t who, int row, bool grey) {
        int x = kPortraitAt.at(who)[0] + kPortraitMiddle - (static_cast<int>(name.size()) * kTileSize / 2);
        for (const char each : name) {
            const auto character = static_cast<std::uint8_t>(each);
            if (character >= kFirstGlyph && std::size_t{character} - kFirstGlyph < graphics.font.size()) {
                const std::size_t glyph = std::size_t{character} - kFirstGlyph;
                Quad letter = quad(on.font[glyph], x, top + row, static_cast<int>(graphics.font[glyph].width),
                                   static_cast<int>(graphics.font[glyph].height));
                letter.see_through = grey ? 3 : 2;
                add(letter);
            }
            x += kTileSize;
        }
    };
    for (std::size_t who = 0; who < hud.names.size(); ++who) {
        name_at(hud.names.at(who), who, kNameRow, false);
        for (std::size_t place = 0; place < hud.watching.at(who).size(); ++place) {
            name_at(hud.watching.at(who).at(place), who, kWatchingRow - (static_cast<int>(place) * kNameLine), true);
        }
    }
    for (std::size_t square = 0; square < kSquares; ++square) {
        const int x = kSquareAt.at(square)[0];
        const int y = top + kSquareAt.at(square)[1];
        if (const std::optional<int> icon = hud.icons.at(square);
            icon && *icon >= 0 && std::cmp_less(*icon, graphics.items.size())) {
            const auto item = static_cast<std::size_t>(*icon);
            whole(graphics.items[item], on.items[item], x, y);
        }
        if (hud.picker.at(square) && kPickerIcon < graphics.items.size()) {
            const assets::IndexedImage& frame = graphics.items[kPickerIcon];
            Quad picker =
                quad(on.items[kPickerIcon], x, y, static_cast<int>(frame.width), static_cast<int>(frame.height));
            picker.mask_dx = static_cast<float>(on.picker_mask.x - on.items[kPickerIcon].x);
            picker.mask_dy = static_cast<float>(on.picker_mask.y - on.items[kPickerIcon].y);
            picker.masked = 1;
            add(picker);
        }
    }
}

// Where each of the view's sprites is in the atlas, those not yet there put into it. False if one has no room.
bool GpuRenderer::State::place_pieces(Page& on, const View& view) {
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
        auto found = on.pieces.find(hash);
        if (found == on.pieces.end()) {
            const auto spot = on.atlas.put(*image);
            if (!spot) {
                return false;
            }
            found = on.pieces.emplace(hash, *spot).first;
        }
        piece_spots[i] = found->second;
    }
    return true;
}

// Everything the picture shows, in the order it is drawn: first what stays above the HUD (`in_playfield` of
// them), then what is drawn in the HUD's place. False when the page has no room for the sprites.
bool GpuRenderer::State::list(Page& on, const View& view, const SceneGraphics& graphics, Picture& picture) {
    const SharedGraphics& common = *on.shared_of;
    picture.quads.clear();
    picture.moves_with.clear();
    picture.width = view.width;
    picture.height = view.height;
    if (!place_pieces(on, view)) {
        return false;
    }
    if (graphics.picture) {
        picture.quads.push_back(quad(on.picture, -view.camera_x, kPictureTop - view.camera_y,
                                     static_cast<int>(graphics.picture->width),
                                     static_cast<int>(graphics.picture->height)));
        picture.moves_with.push_back(kWithCamera);
    }
    list_tiles(on, view, graphics, false, picture);
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
        picture.quads.push_back(piece);
        picture.moves_with.push_back(static_cast<std::int32_t>(i));
    }
    list_tiles(on, view, graphics, true, picture);
    for (int row = 0; row < std::min(view.height / kTileSize, kTextRows); ++row) {
        for (int column = 0; column < std::min(view.width / kTileSize, kTextColumns); ++column) {
            const std::uint8_t character =
                view.text[(static_cast<std::size_t>(row) * kTextColumns) + static_cast<std::size_t>(column)];
            if (character < kFirstGlyph || std::size_t{character} - kFirstGlyph >= common.font.size()) {
                continue;
            }
            const std::size_t glyph = std::size_t{character} - kFirstGlyph;
            Quad letter = quad(on.font[glyph], view.text_x + (column * kTileSize), view.text_y + (row * kTileSize),
                               static_cast<int>(common.font[glyph].width), static_cast<int>(common.font[glyph].height));
            letter.see_through = 1;
            picture.quads.push_back(letter);
            picture.moves_with.push_back(kWithCamera);
        }
    }
    picture.in_playfield = picture.quads.size();
    picture.backdrop.reset();
    if (graphics.backdrop && on.backdrop && view.map != nullptr && view.map->width > 0) {
        const Backdrop& backdrop = *graphics.backdrop;
        picture.backdrop =
            Picture::Behind{.at = *on.backdrop,
                            .width = static_cast<int>(backdrop.image.width),
                            .height = static_cast<int>(backdrop.image.height),
                            .scroll_x = backdrop.scroll_x,
                            .scroll_y = backdrop.scroll_y,
                            .map = {-view.camera_x, -view.camera_y, view.map->pixel_width() - view.camera_x,
                                    view.map->pixel_height() - view.camera_y}};
    }
    if (graphics.hud_picture) {  // its first 64 rows, in the HUD's place
        picture.quads.push_back(quad(on.hud_picture, 0, view.height,
                                     std::min(kScreenWidth, static_cast<int>(graphics.hud_picture->width)),
                                     std::min(kHudHeight, static_cast<int>(graphics.hud_picture->height))));
        picture.moves_with.push_back(kStays);
    }
    if (view.hud) {
        list_hud(on, *view.hud, common, view.height, picture);
    }
    return true;
}

GpuRenderer::GpuRenderer(std::unique_ptr<State> state) : state_{std::move(state)} {}

std::size_t GpuRenderer::pages_set_up() const noexcept {
    return state_->pages_set_up;
}

bool GpuRenderer::waits_for_refresh() const noexcept {
    return !state_->mailbox;
}

const GpuRenderer::ShowTimes& GpuRenderer::show_times() const noexcept {
    return state_->times;
}

std::string GpuRenderer::device_name() const {
    const SDL_PropertiesID properties = SDL_GetGPUDeviceProperties(state_->device);
    const char* name = SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "an unnamed device");
    const char* driver = SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "");
    const char* version = SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "");
    return std::string{name} + " (" + SDL_GetGPUDeviceDriver(state_->device) + ", " + driver + " " + version + ")";
}
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
    if (SDL_WindowSupportsGPUPresentMode(state->device, window, SDL_GPU_PRESENTMODE_MAILBOX) &&
        SDL_SetGPUSwapchainParameters(state->device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      SDL_GPU_PRESENTMODE_MAILBOX)) {
        state->mailbox = true;
    }
    if (auto ready = state->set_up(); !ready) {
        return std::unexpected{ready.error()};
    }
    return std::unique_ptr<GpuRenderer>{new GpuRenderer{std::move(state)}};
}

// The view's picture as quads on a page of the atlas, what the page has new sent to the card with `commands`.
std::expected<GpuRenderer::State::Picture, std::string> GpuRenderer::State::make(SDL_GPUCommandBuffer* commands,
                                                                                 const View& view,
                                                                                 const SceneGraphics& graphics,
                                                                                 const SharedGraphics& common) {
    // The page in use while it holds these pictures (a scene without a number: never); else a free one, set up
    // for them. One that the sprites have filled is left as it is, for the pictures drawn from it, and a free
    // one used instead.
    Page* on = page < pages.size() ? pages[page].get() : nullptr;
    if (on == nullptr || on->shared_of != &common || graphics.id == 0 || on->scene_id != graphics.id) {
        auto set_up = set_up_page(graphics, common);
        if (!set_up) {
            return std::unexpected{set_up.error()};
        }
        on = *set_up;
    }
    Picture picture;
    if (!list(*on, view, graphics, picture)) {
        auto set_up = set_up_page(graphics, common);
        if (!set_up) {
            return std::unexpected{set_up.error()};
        }
        on = *set_up;
        if (!list(*on, view, graphics, picture)) {
            return std::unexpected{std::string{"the atlas has no room for the sprites"}};
        }
    }
    picture.page = page;
    if (const auto [top, bottom] = on->atlas.take_changed(); top < bottom) {
        const std::size_t bytes = static_cast<std::size_t>(bottom - top) * kAtlasSize;
        void* mapped = SDL_MapGPUTransferBuffer(device, atlas_upload, true);
        if (mapped == nullptr) {
            return sdl_error("the atlas");
        }
        std::memcpy(mapped, on->atlas.at(0, top), bytes);
        SDL_UnmapGPUTransferBuffer(device, atlas_upload);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        const SDL_GPUTextureTransferInfo from{
            .transfer_buffer = atlas_upload, .offset = 0, .pixels_per_row = kAtlasSize, .rows_per_layer = 0};
        const SDL_GPUTextureRegion to{.texture = on->texture,
                                      .mip_level = 0,
                                      .layer = 0,
                                      .x = 0,
                                      .y = static_cast<Uint32>(top),
                                      .z = 0,
                                      .w = kAtlasSize,
                                      .h = static_cast<Uint32>(bottom - top),
                                      .d = 1};
        SDL_UploadToGPUTexture(copy, &from, &to, false);
        SDL_EndGPUCopyPass(copy);
        ++since.uploads;
        since.uploaded_bytes += bytes;
    }
    return picture;
}

// A picture's quads drawn onto a texture of palette indices, `scale` pixels a pixel of the picture. With a
// `motion` of a tick's picture, what moves is moved as it is `between` (0 to 1) into the refresh it is shown for.
std::expected<void, std::string> GpuRenderer::State::render(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* onto,
                                                            const Picture& picture, int scale, const Motion* motion,
                                                            float between) {
    quads = picture.quads;
    if (motion != nullptr && motion->phase >= 0) {
        const std::array<float, 2> camera = camera_moved(*motion, between);
        std::vector<std::array<float, 2>> sprites(motion->sprites.size());
        for (std::size_t i = 0; i < sprites.size(); ++i) {
            sprites[i] = {moved_since(motion->sprites[i][0], motion->phase, between),
                          moved_since(motion->sprites[i][1], motion->phase, between)};
        }
        for (std::size_t i = 0; i < quads.size(); ++i) {
            const std::int32_t with = picture.moves_with[i];
            if (with == kStays) {
                continue;
            }
            quads[i].left -= camera[0];
            quads[i].top -= camera[1];
            if (with >= 0 && std::cmp_less(with, sprites.size())) {
                quads[i].left += sprites[static_cast<std::size_t>(with)][0];
                quads[i].top += sprites[static_cast<std::size_t>(with)][1];
            }
        }
    }
    if (auto room = room_for_quads(); !room) {
        return room;
    }
    if (!quads.empty()) {
        const std::size_t bytes = quads.size() * sizeof(Quad);
        void* mapped = SDL_MapGPUTransferBuffer(device, quads_upload, true);
        if (mapped == nullptr) {
            return sdl_error("the quads");
        }
        std::memcpy(mapped, quads.data(), bytes);
        SDL_UnmapGPUTransferBuffer(device, quads_upload);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        const SDL_GPUTransferBufferLocation from{.transfer_buffer = quads_upload, .offset = 0};
        const SDL_GPUBufferRegion to{.buffer = quads_buffer, .offset = 0, .size = static_cast<Uint32>(bytes)};
        SDL_UploadToGPUBuffer(copy, &from, &to, true);
        SDL_EndGPUCopyPass(copy);
    }

    SDL_GPUColorTargetInfo colour{};
    colour.texture = onto;
    colour.load_op = SDL_GPU_LOADOP_CLEAR;
    colour.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &colour, 1, nullptr);
    if (!quads.empty() && picture.page < pages.size()) {
        constexpr std::uint32_t kCorners = 6;
        const int wide = std::max(picture.width, kScreenWidth);
        const int high = picture.height + kHudHeight;
        // The quads are in the picture's pixels; the target has `scale` times as many.
        const std::array<float, 2> size{static_cast<float>(wide), static_cast<float>(high)};
        const SDL_GPUViewport viewport{.x = 0,
                                       .y = 0,
                                       .w = static_cast<float>(wide * scale),
                                       .h = static_cast<float>(high * scale),
                                       .min_depth = 0,
                                       .max_depth = 1};
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_SetGPUViewport(pass, &viewport);
        SDL_PushGPUVertexUniformData(commands, 0, size.data(), sizeof(size));
        const SDL_GPUBufferBinding buffer{.buffer = quads_buffer, .offset = 0};
        SDL_BindGPUVertexBuffers(pass, 0, &buffer, 1);
        const SDL_GPUTextureSamplerBinding from_atlas{.texture = pages[picture.page]->texture, .sampler = sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &from_atlas, 1);
        const SDL_Rect playfield{.x = 0, .y = 0, .w = picture.width * scale, .h = picture.height * scale};
        const SDL_Rect whole{.x = 0, .y = 0, .w = wide * scale, .h = high * scale};
        if (picture.in_playfield != 0) {
            SDL_SetGPUScissor(pass, &playfield);
            SDL_DrawGPUPrimitives(pass, kCorners, static_cast<Uint32>(picture.in_playfield), 0, 0);
        }
        if (picture.in_playfield != quads.size()) {
            SDL_SetGPUScissor(pass, &whole);
            SDL_DrawGPUPrimitives(pass, kCorners, static_cast<Uint32>(quads.size() - picture.in_playfield), 0,
                                  static_cast<Uint32>(picture.in_playfield));
        }
    }
    SDL_EndGPURenderPass(pass);
    return {};
}

// The minimap's pixels into its texture, which is made anew when the size is another. False if that failed.
bool GpuRenderer::State::upload_over(SDL_GPUCommandBuffer* commands, const Minimap& minimap) {
    const std::size_t bytes = static_cast<std::size_t>(minimap.width) * static_cast<std::size_t>(minimap.height) * 4;
    if (minimap.width <= 0 || minimap.height <= 0 || minimap.pixels.size() != bytes) {
        return false;
    }
    if (minimap.width != over_width || minimap.height != over_height) {
        SDL_ReleaseGPUTexture(device, over_texture);
        SDL_ReleaseGPUTransferBuffer(device, over_upload);
        over_texture =
            texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, minimap.width, minimap.height);
        const SDL_GPUTransferBufferCreateInfo made{
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = static_cast<Uint32>(bytes), .props = 0};
        over_upload = SDL_CreateGPUTransferBuffer(device, &made);
        over_width = over_texture != nullptr && over_upload != nullptr ? minimap.width : 0;
        over_height = over_width != 0 ? minimap.height : 0;
    }
    void* mapped = over_width != 0 ? SDL_MapGPUTransferBuffer(device, over_upload, true) : nullptr;
    if (mapped == nullptr) {
        return false;
    }
    std::memcpy(mapped, minimap.pixels.data(), bytes);
    SDL_UnmapGPUTransferBuffer(device, over_upload);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTextureTransferInfo from{.transfer_buffer = over_upload,
                                          .offset = 0,
                                          .pixels_per_row = static_cast<Uint32>(minimap.width),
                                          .rows_per_layer = 0};
    const SDL_GPUTextureRegion to{.texture = over_texture,
                                  .mip_level = 0,
                                  .layer = 0,
                                  .x = 0,
                                  .y = 0,
                                  .z = 0,
                                  .w = static_cast<Uint32>(minimap.width),
                                  .h = static_cast<Uint32>(minimap.height),
                                  .d = 1};
    SDL_UploadToGPUTexture(copy, &from, &to, true);
    SDL_EndGPUCopyPass(copy);
    return true;
}

// The overlay's pixels into its texture (made the first time), if it doesn't have them yet. False if that failed.
bool GpuRenderer::State::upload_overlay(SDL_GPUCommandBuffer* commands) {
    constexpr std::size_t kBytes = std::size_t{kScreenWidth} * kScreenHeight * 4;
    if (overlay.size() != kBytes) {
        return false;
    }
    if (overlay_sent) {
        return true;
    }
    if (overlay_texture == nullptr) {
        overlay_texture =
            texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, kScreenWidth, kScreenHeight);
        const SDL_GPUTransferBufferCreateInfo made{
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = static_cast<Uint32>(kBytes), .props = 0};
        overlay_upload = SDL_CreateGPUTransferBuffer(device, &made);
    }
    void* mapped = overlay_texture != nullptr && overlay_upload != nullptr
                       ? SDL_MapGPUTransferBuffer(device, overlay_upload, true)
                       : nullptr;
    if (mapped == nullptr) {
        return false;
    }
    std::memcpy(mapped, overlay.data(), kBytes);
    SDL_UnmapGPUTransferBuffer(device, overlay_upload);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTextureTransferInfo from{
        .transfer_buffer = overlay_upload, .offset = 0, .pixels_per_row = kScreenWidth, .rows_per_layer = 0};
    const SDL_GPUTextureRegion to{.texture = overlay_texture,
                                  .mip_level = 0,
                                  .layer = 0,
                                  .x = 0,
                                  .y = 0,
                                  .z = 0,
                                  .w = kScreenWidth,
                                  .h = kScreenHeight,
                                  .d = 1};
    SDL_UploadToGPUTexture(copy, &from, &to, true);
    SDL_EndGPUCopyPass(copy);
    overlay_sent = true;
    return true;
}

// What stops light into its texture, made anew when the size is another. False if that failed.
bool GpuRenderer::State::upload_ground(SDL_GPUCommandBuffer* commands, const Ground& ground) {
    const std::size_t bytes = static_cast<std::size_t>(ground.width) * static_cast<std::size_t>(ground.height);
    if (ground.width <= 0 || ground.height <= 0 || ground.solid.size() != bytes) {
        return false;
    }
    if (ground.width != ground_width || ground.height != ground_height) {
        SDL_ReleaseGPUTexture(device, ground_texture);
        ground_texture =
            texture(SDL_GPU_TEXTUREFORMAT_R8_UINT, SDL_GPU_TEXTUREUSAGE_SAMPLER, ground.width, ground.height);
        ground_width = ground_texture != nullptr ? ground.width : 0;
        ground_height = ground_texture != nullptr ? ground.height : 0;
        if (ground_texture == nullptr) {
            ground_texture = texture(SDL_GPU_TEXTUREFORMAT_R8_UINT, SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, 1);
            ground_id = 0;
            return false;
        }
    }
    const SDL_GPUTransferBufferCreateInfo made{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = static_cast<Uint32>(bytes), .props = 0};
    SDL_GPUTransferBuffer* upload = SDL_CreateGPUTransferBuffer(device, &made);
    void* mapped = upload != nullptr ? SDL_MapGPUTransferBuffer(device, upload, false) : nullptr;
    if (mapped == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, upload);
        return false;
    }
    std::memcpy(mapped, ground.solid.data(), bytes);
    SDL_UnmapGPUTransferBuffer(device, upload);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTextureTransferInfo from{.transfer_buffer = upload,
                                          .offset = 0,
                                          .pixels_per_row = static_cast<Uint32>(ground.width),
                                          .rows_per_layer = 0};
    const SDL_GPUTextureRegion to{.texture = ground_texture,
                                  .mip_level = 0,
                                  .layer = 0,
                                  .x = 0,
                                  .y = 0,
                                  .z = 0,
                                  .w = static_cast<Uint32>(ground.width),
                                  .h = static_cast<Uint32>(ground.height),
                                  .d = 1};
    SDL_UploadToGPUTexture(copy, &from, &to, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(device, upload);
    ground_id = ground.id;
    return true;
}

// A kept picture onto a target of `width` x `height`: as large as whole pixels allow, in the middle, the rest
// black. It is drawn now, as large as it is shown, and what moves between ticks is moved as it is `between` (0
// to 1) into the refresh it is shown for.
std::expected<void, std::string> GpuRenderer::State::show(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* onto,
                                                          int width, int height, const Screen& screen,
                                                          const Minimap* minimap, bool own, float between, int whole) {
    times.samples = 0;
    std::shared_ptr<const Picture> kept_picture;
    if (screen.kept >= 0) {
        const Kept& found = kept.at(static_cast<std::size_t>(screen.kept) % kKept);
        if (found.number == screen.kept) {
            kept_picture = found.picture;
        }
    }
    const Picture* picture = kept_picture.get();
    if (!own && picture != nullptr) {
        // In a window the pictures before this one and the one shown before it won't be shown again. (A rewind's
        // last step shows its two pictures newest first.)
        const std::int64_t done = std::min(screen.kept, shown_kept);
        for (Kept& older : kept) {
            if (older.number < done) {
                older = {};
            }
        }
        shown_kept = screen.kept;
    }
    // The screen's look (never in a picture of a whole level). crt and xbr draw all of it into a texture of our own
    // first (onto which the pipelines for our own format draw), in the game's whole pixels (nothing moved between
    // ticks), and from there onto the target in a last pass that finds the picture's pixels on that grid.
    const Look shown_look = whole == 0 ? look : Look::kNone;
    const bool crt = shown_look == Look::kCrt || shown_look == Look::kXbr;
    const bool whole_pixels = shown_look == Look::kCrt || shown_look == Look::kXbr;
    const bool own_format = own || crt;
    SDL_GPUGraphicsPipeline* with = own_format ? show_pipeline_own : show_pipeline;
    // The HUD slides aside first, and the minimap fades in when it is there; taken away, the minimap fades out and
    // then the HUD slides back. (A picture read back is shown as it ends up.) Fading out, it is the minimap as it
    // last was.
    // A scene's fades darken it with the picture; in a picture all dark it is as it will be (the next scene's
    // rises with it, or there is none).
    const bool wanted = minimap != nullptr && upload_over(commands, *minimap);
    float passed = 1;  // refreshes since the last picture shown
    if (!own) {
        const std::uint64_t now = SDL_GetTicksNS();
        passed = last_show_ns == 0
                     ? 1.0F
                     : std::min(static_cast<float>(static_cast<double>(now - last_show_ns) / 1e9 * kDisplayHz),
                                kMostRefreshesAPicture);
        last_show_ns = now;
    }
    if (own || screen.bright <= 0.0F) {
        made_room = faded_in = wanted ? 1.0F : 0.0F;
    } else if (wanted) {
        made_room = std::min(made_room + (passed / kSlideRefreshes), 1.0F);
        faded_in = made_room >= 1.0F ? std::min(faded_in + (passed / kFadeRefreshes), 1.0F) : 0.0F;
    } else {
        faded_in = std::max(faded_in - (passed / kFadeRefreshes), 0.0F);
        made_room = faded_in <= 0.0F ? std::max(made_room - (passed / kSlideRefreshes), 0.0F) : made_room;
    }
    const float seen = faded_in * std::clamp(screen.bright, 0.0F, 1.0F);
    const bool with_minimap = seen > 0.0F && over_width != 0;
    const bool with_overlay = whole == 0 && upload_overlay(commands);
    void* mapped = SDL_MapGPUTransferBuffer(device, palette_upload, true);
    if (mapped == nullptr) {
        return sdl_error("the palette");
    }
    auto* colours = static_cast<std::uint8_t*>(mapped);
    for (std::size_t i = 0; i < kColours; ++i) {
        const assets::Rgb& colour = screen.palette.at(i);
        const std::array<std::uint8_t, 4> rgba{colour.r, colour.g, colour.b, screen.glow.at(i)};  // (the glow)
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
    const bool grounded =
        screen.ground != nullptr && (screen.ground->id == ground_id || upload_ground(commands, *screen.ground));

    // The original's screen as large as the window holds it, its shape kept (the user, 2026-11-26: not only as large
    // as whole pixels allow, with black around it). Shown at a size not a whole number of times its own, it is drawn
    // the next whole number of times and shown a little smaller: a pixel of the window is the mean of what it covers.
    const float scale =
        std::max(1.0F, std::min(static_cast<float>(width) / kScreenWidth, static_cast<float>(height) / kScreenHeight));
    const auto left = static_cast<int>(std::round((static_cast<float>(width) - (kScreenWidth * scale)) / 2));
    const auto top = static_cast<int>(std::round((static_cast<float>(height) - (kScreenHeight * scale)) / 2));
    // The playfield's part with the HUD under it, both at one scale: as large as the original's screen's place
    // holds them. The original's view fills it; a larger one makes everything smaller, the HUD too.
    const float screen_width = kScreenWidth * scale;
    const float screen_height = kScreenHeight * scale;
    const auto picture_width = static_cast<float>(picture != nullptr ? std::max(picture->width, kScreenWidth) : 0);
    const auto picture_height = static_cast<float>(picture != nullptr ? picture->height : 0);
    const Screen::Part shown{.x = std::clamp(screen.part.x, 0.0F, picture_width),
                             .y = std::clamp(screen.part.y, 0.0F, picture_height),
                             .width = std::clamp(screen.part.width, 1.0F, std::max(picture_width, 1.0F)),
                             .height = std::clamp(screen.part.height, 1.0F, std::max(picture_height, 1.0F))};
    // A view larger than the original's is as high as the whole screen, and the HUD lies over its bottom.
    const bool over = shown.height > kPlayfieldHeight;
    const float fit = whole > 0
                          ? static_cast<float>(whole)
                          : std::min(screen_width / std::max(shown.width, static_cast<float>(kScreenWidth)),
                                     screen_height / (over ? std::max(shown.height, static_cast<float>(kScreenHeight))
                                                           : shown.height + kHudHeight));
    // Drawn as many pixels a pixel as it is shown at (rounded up: shown a little smaller, pixels are mixed).
    const int drawn = std::clamp(static_cast<int>(std::ceil(fit - 0.01F)), 1, kMostScale);
    if (picture != nullptr) {
        const int wide = std::max(picture->width, kScreenWidth) * drawn;
        const int high = (picture->height + kHudHeight) * drawn;
        if (work == nullptr || work_width != wide || work_height != high) {
            SDL_ReleaseGPUTexture(device, work);
            work = texture(SDL_GPU_TEXTUREFORMAT_R8_UINT,
                           SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, wide, high);
            work_width = work != nullptr ? wide : 0;
            work_height = work != nullptr ? high : 0;
            in_work.reset();
            times.remade = true;
            if (work == nullptr) {
                return sdl_error("the picture to show");
            }
        }
        times.drawn_scale = drawn;
        times.width = wide;
        times.height = high;
        // A fade's pictures are one picture in other colours, and a picture without a tick's motion is the same
        // at any moment: drawn once, however many times the window refreshes while it is shown.
        const bool still = whole_pixels || screen.motion.phase < 0 || screen.motion.phase > 2;
        times.redrawn = !still || in_work != kept_picture || in_work_scale != drawn || in_work_look != shown_look;
        if (times.redrawn) {
            if (auto rendered =
                    render(commands, work, *picture, drawn, whole_pixels ? nullptr : &screen.motion, between);
                !rendered) {
                in_work.reset();
                return rendered;
            }
            in_work = still ? kept_picture : nullptr;
            in_work_scale = drawn;
            in_work_look = shown_look;
        }
    }
    if (crt && (crt_source == nullptr || crt_width != width || crt_height != height)) {
        SDL_ReleaseGPUTexture(device, crt_source);
        crt_source = texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                             SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, width, height);
        crt_width = crt_source != nullptr ? width : 0;
        crt_height = crt_source != nullptr ? height : 0;
        if (crt_source == nullptr) {
            return sdl_error("the picture for the screen's look");
        }
    }
    std::array<float, 2> grid{static_cast<float>(left), static_cast<float>(top)};
    SDL_GPUColorTargetInfo colour{};
    colour.texture = crt ? crt_source : onto;
    colour.clear_color = {.r = 0, .g = 0, .b = 0, .a = 1};
    colour.load_op = SDL_GPU_LOADOP_CLEAR;
    colour.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &colour, 1, nullptr);
    if (picture != nullptr) {  // none kept (yet): black
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
            std::array<float, 4> backdrop;  // in the atlas: left, top, width, height; a width of 0: none
            std::array<float, 4> field;     // where the map is, in the picture's pixels: left, top, right, bottom
            std::array<float, 2> offset;    // where the picture's top left corner is in the backdrop
            float samples;
            float scale;               // the drawn picture's pixels a pixel of it
            std::array<float, 4> lit;  // the Modern visuals' light: how many lights, a glow's reach, the camera moved
            std::array<float, 4> ground;  // where the picture's corner is in the level; what stops light: its size
            std::array<std::array<float, 4>, 2 * kMostLights> lights;
        };
        // The Modern visuals' light (effects.hpp), for the playfield.
        const std::array<float, 2> camera_now =
            whole_pixels ? std::array<float, 2>{} : camera_moved(screen.motion, between);
        std::array<float, 4> lit{};
        std::array<std::array<float, 4>, 2 * kMostLights> lights{};
        const std::size_t light_count = std::min(screen.lights.size(), kMostLights);
        for (std::size_t i = 0; i < light_count; ++i) {
            const Light& light = screen.lights[i];
            lights.at(2 * i) = {light.x, light.y, light.half_width, light.half_height};
            lights.at((2 * i) + 1) = {light.colour[0], light.colour[1], light.colour[2], std::max(light.reach, 1.0F)};
        }
        const bool glows = std::ranges::any_of(screen.glow, [](std::uint8_t glow) { return glow != 0; });
        lit = {static_cast<float>(light_count), glows ? kGlowReach : 0.0F, camera_now[0], camera_now[1]};
        const std::array<float, 4> ground_at{
            static_cast<float>(screen.level_x) + camera_now[0], static_cast<float>(screen.level_y) + camera_now[1],
            grounded ? static_cast<float>(ground_width) : 0.0F, grounded ? static_cast<float>(ground_height) : 0.0F};
        // The backdrop (Remaster rules' parallax, backdrop.hpp): placed by the camera without the shake, as it is
        // at this moment (what moves between ticks moved too), and what moves by itself by the refreshes.
        std::array<float, 4> behind{};
        std::array<float, 4> field{};
        std::array<float, 2> offset{};
        if (picture->backdrop && picture->page < pages.size()) {
            const Picture::Behind& backdrop = *picture->backdrop;
            const std::array<float, 2> moved =
                whole_pixels ? std::array<float, 2>{} : camera_moved(screen.motion, between);
            // (A picture is shown for one refresh, `between` into it: what moves by itself goes on evenly.)
            const double refresh = static_cast<double>(screen.refresh) + static_cast<double>(between);
            behind = {static_cast<float>(backdrop.at.x), static_cast<float>(backdrop.at.y),
                      static_cast<float>(backdrop.width), static_cast<float>(backdrop.height)};
            field = {static_cast<float>(backdrop.map[0]) - moved[0], static_cast<float>(backdrop.map[1]) - moved[1],
                     static_cast<float>(backdrop.map[2]) - moved[0], static_cast<float>(backdrop.map[3]) - moved[1]};
            for (std::size_t axis = 0; axis < 2; ++axis) {
                offset.at(axis) = static_cast<float>(backdrop_offset(
                    axis == 0 ? backdrop.scroll_x : backdrop.scroll_y,
                    static_cast<double>(screen.motion.scroll.at(axis)) + static_cast<double>(moved.at(axis)), refresh,
                    axis == 0 ? backdrop.width : backdrop.height));
            }
        }
        SDL_BindGPUGraphicsPipeline(pass, with);
        SDL_GPUTexture* atlas = picture->page < pages.size() ? pages[picture->page]->texture : work;
        const std::array<SDL_GPUTextureSamplerBinding, 4> pictures{{{.texture = work, .sampler = sampler},
                                                                    {.texture = palette_texture, .sampler = sampler},
                                                                    {.texture = atlas, .sampler = sampler},
                                                                    {.texture = ground_texture, .sampler = sampler}}};
        SDL_BindGPUFragmentSamplers(pass, 0, pictures.data(), pictures.size());
        const auto part = [&](std::array<float, 4> dest, std::array<float, 4> source, std::array<float, 4> within,
                              bool playfield) {
            // (In the picture's pixels; it is drawn `drawn` times as large.)
            for (std::size_t i = 0; i < 4; ++i) {
                source.at(i) *= static_cast<float>(drawn);
                within.at(i) *= static_cast<float>(drawn);
            }
            // Shown smaller than it is, a pixel is the mean of what it covers (up to 4 x 4 of the picture's).
            const float covers = source[2] / dest[2];
            const Placed placed{.dest = dest,
                                .source = source,
                                .size = {static_cast<float>(width), static_cast<float>(height)},
                                .unused = {}};
            const Sampled sampled{.within = within,
                                  .backdrop = playfield ? behind : std::array<float, 4>{},
                                  .field = field,
                                  .offset = offset,
                                  .samples = std::clamp(std::ceil(covers - 0.01F), 1.0F, kMostSamples),
                                  .scale = static_cast<float>(drawn),
                                  .lit = playfield ? lit : std::array<float, 4>{},
                                  .ground = ground_at,
                                  .lights = lights};
            times.samples = std::max(times.samples, static_cast<int>(sampled.samples));
            SDL_PushGPUVertexUniformData(commands, 0, &placed, sizeof(placed));
            SDL_PushGPUFragmentUniformData(commands, 0, &sampled, sizeof(sampled));
            SDL_DrawGPUPrimitives(pass, kCorners, 1, 0, 0);
        };
        if (whole > 0) {
            part({0, 0, static_cast<float>(width), static_cast<float>(height)},
                 {0, 0, static_cast<float>(width) / fit, static_cast<float>(height) / fit},
                 {0, 0, picture_width, picture_height}, true);
        }
        const float field_height = shown.height * fit;
        // A larger view less high than the screen (a level as low as that) is in the middle of what is above the
        // HUD.
        const float above_hud = screen_height - (kHudHeight * fit);
        const float all_top =
            over ? static_cast<float>(top) + std::max(0.0F, (above_hud - field_height) / 2)
                 : static_cast<float>(top) + ((screen_height - (field_height + (kHudHeight * fit))) / 2);
        const auto middle = [&](float wide) { return static_cast<float>(left) + ((screen_width - wide) / 2); };
        if (whole == 0) {
            part({middle(shown.width * fit), all_top, shown.width * fit, field_height},
                 {shown.x, shown.y, shown.width, shown.height}, {0, 0, picture_width, picture_height}, true);
        }
        // Where the picture's pixels lie (the HUD's too, but while it slides aside): for xbr's last pass.
        grid = {middle(shown.width * fit) - ((shown.x - std::floor(shown.x)) * fit),
                all_top - ((shown.y - std::floor(shown.y)) * fit)};
        const auto hud_top = picture_height;
        const float hud_at =
            over ? static_cast<float>(top) + screen_height - (kHudHeight * fit) : all_top + field_height;
        // With the minimap in the bottom left corner the HUD makes room, where it would be under it: it goes right
        // by a column of its stone tiles. Shown smaller, it is clear of the corner as it is, in the middle.
        float hud_left = middle(kScreenWidth * fit);
        const float aside = static_cast<float>(left) + (kMinimapSide * scale) - (kHudSpare * fit);
        if (aside > hud_left) {
            const float eased = made_room * made_room * (3.0F - (2.0F * made_room));
            hud_left += (aside - hud_left) * eased;
        }
        const float hud_seen = std::min(kScreenWidth * fit, static_cast<float>(left) + screen_width - hud_left) / fit;
        if (whole == 0) {
            part({hud_left, hud_at, hud_seen * fit, kHudHeight * fit}, {0, hud_top, hud_seen, kHudHeight},
                 {0, hud_top, kScreenWidth, hud_top + kHudHeight}, false);
        }
    }
    if (with_minimap) {
        // A square of the minimap around the middle of what the view shows, in the screen's bottom left corner. It
        // doesn't reach past the level's edges unless the level is smaller than the square.
        constexpr std::uint32_t kCorners = 6;
        constexpr float kToMinimap = static_cast<float>(kMinimapCell) / kCellSize;
        struct Placed {  // gpu_show.vert
            std::array<float, 4> dest;
            std::array<float, 4> source;
            std::array<float, 2> size;
            std::array<float, 2> unused;
        };
        struct Over {  // gpu_over.frag
            std::array<float, 4> size;
            std::array<float, 4> window;
            std::array<float, 4> view;
            std::array<std::array<float, 4>, 3> vikings;
        };
        const auto side = static_cast<float>(kMinimapCells * kMinimapCell);
        const std::array<float, 4> view{
            (static_cast<float>(screen.level_x) + screen.part.x) * kToMinimap,
            (static_cast<float>(screen.level_y) + screen.part.y) * kToMinimap,
            (static_cast<float>(screen.level_x) + screen.part.x + screen.part.width) * kToMinimap,
            (static_cast<float>(screen.level_y) + screen.part.y + screen.part.height) * kToMinimap};
        const auto start_of = [side](float low, float high, int all) {
            const auto whole = static_cast<float>(all);
            return whole >= side ? std::clamp(((low + high) / 2) - (side / 2), 0.0F, whole - side) : (whole - side) / 2;
        };
        const float source_x = start_of(view[0], view[2], over_width);
        const float source_y = start_of(view[1], view[3], over_height);
        const float shown_side = kMinimapSide * scale;
        const Placed placed{.dest = {static_cast<float>(left), static_cast<float>(top) + screen_height - shown_side,
                                     shown_side, shown_side},
                            .source = {source_x, source_y, side, side},
                            .size = {static_cast<float>(width), static_cast<float>(height)},
                            .unused = {}};
        Over how{.size = {static_cast<float>(over_width), static_cast<float>(over_height), side / kMinimapSide, seen},
                 .window = {source_x, source_y, source_x + side, source_y + side},
                 .view = view,
                 .vikings = {}};
        for (std::size_t who = 0; who < how.vikings.size(); ++who) {
            const Screen::Viking& viking = screen.vikings.at(who);
            how.vikings.at(who) = {static_cast<float>(viking.x) * kToMinimap, static_cast<float>(viking.y) * kToMinimap,
                                   viking.there ? 1.0F : 0.0F, viking.played ? 1.0F : 0.0F};
        }
        SDL_BindGPUGraphicsPipeline(pass, own_format ? over_pipeline_own : over_pipeline);
        SDL_PushGPUVertexUniformData(commands, 0, &placed, sizeof(placed));
        SDL_PushGPUFragmentUniformData(commands, 0, &how, sizeof(how));
        const SDL_GPUTextureSamplerBinding over_picture{.texture = over_texture, .sampler = smooth};
        SDL_BindGPUFragmentSamplers(pass, 0, &over_picture, 1);
        SDL_DrawGPUPrimitives(pass, kCorners, 1, 0, 0);
        if (screen.rewind_left) {
            // Along its bottom, the rewind's bar: what is left of the level's rewind. It comes and goes with it.
            struct Gauge {  // gpu_gauge.frag
                std::array<float, 4> size;
                std::array<float, 4> left;
            };
            const float pixel = scale;
            const Placed bar{.dest = {placed.dest[0] + (kGaugeAt[0] * pixel), placed.dest[1] + (kGaugeAt[1] * pixel),
                                      kGaugeAt[2] * pixel, kGaugeAt[3] * pixel},
                             .source = {0, 0, kGaugeAt[2], kGaugeAt[3]},
                             .size = placed.size,
                             .unused = {}};
            const Gauge gauge{.size = {kGaugeAt[2], kGaugeAt[3], kGaugeRadius, 1},
                              .left = {std::clamp(*screen.rewind_left, 0.0F, 1.0F), seen, 0, 0}};
            SDL_BindGPUGraphicsPipeline(pass, own_format ? gauge_pipeline_own : gauge_pipeline);
            SDL_PushGPUVertexUniformData(commands, 0, &bar, sizeof(bar));
            SDL_PushGPUFragmentUniformData(commands, 0, &gauge, sizeof(gauge));
            SDL_DrawGPUPrimitives(pass, kCorners, 1, 0, 0);
        }
    }
    // Time running backwards: two triangles pointing left in the screen's top right corner, flashing (about twice
    // a second; in a picture read back they are there). First dark, a pixel down and right, then light over it.
    const float marked_before = marked;
    marked = screen.rewinding ? marked + passed : 0;
    if (whole == 0 && screen.rewinding && (own || static_cast<int>(marked_before / kMarkRefreshes) % 2 == 0)) {
        constexpr std::uint32_t kCorners = 6;
        constexpr int kWide = 18;  // the two together, in the original's pixels
        constexpr int kHigh = 11;
        constexpr int kFromEdge = 8;
        struct Placed {  // gpu_show.vert
            std::array<float, 4> dest;
            std::array<float, 4> source;
            std::array<float, 2> size;
            std::array<float, 2> unused;
        };
        SDL_BindGPUGraphicsPipeline(pass, own_format ? mark_pipeline_own : mark_pipeline);
        const auto mark = [&](int nudge, const std::array<float, 4>& tint) {
            const Placed placed{.dest = {static_cast<float>(left) +
                                             (static_cast<float>(kScreenWidth - kFromEdge - kWide + nudge) * scale),
                                         static_cast<float>(top) + (static_cast<float>(kFromEdge + nudge) * scale),
                                         kWide * scale, kHigh * scale},
                                .source = {0, 0, 2, 1},
                                .size = {static_cast<float>(width), static_cast<float>(height)},
                                .unused = {}};
            SDL_PushGPUVertexUniformData(commands, 0, &placed, sizeof(placed));
            SDL_PushGPUFragmentUniformData(commands, 0, tint.data(), sizeof(tint));
            SDL_DrawGPUPrimitives(pass, kCorners, 1, 0, 0);
        };
        mark(1, {0, 0, 0, 0.7F});
        mark(0, {1, 1, 1, 0.95F});
    }
    if (with_overlay) {
        // The menu, over all of it, in the game's pixels as large as they are shown (smaller zoomed out, as the HUD
        // is), but never smaller than at the zoom's middle step, 480 pixels across (the user: at twice the view it is
        // too small); in the middle of the screen, on the picture's grid where its pixels are the picture's.
        constexpr std::uint32_t kCorners = 6;
        struct Placed {  // gpu_show.vert
            std::array<float, 4> dest;
            std::array<float, 4> source;
            std::array<float, 2> size;
            std::array<float, 2> unused;
        };
        constexpr float kSmallestAcross = 480;
        const float pixel = std::max(fit, screen_width / kSmallestAcross);
        const float panel_width = kScreenWidth * pixel;
        const float panel_height = kScreenHeight * pixel;
        const auto on_grid = [pixel](float at, float first) {
            return first + (std::round((at - first) / pixel) * pixel);
        };
        const Placed placed{.dest = {on_grid(static_cast<float>(left) + ((screen_width - panel_width) / 2), grid[0]),
                                     on_grid(static_cast<float>(top) + ((screen_height - panel_height) / 2), grid[1]),
                                     panel_width, panel_height},
                            .source = {0, 0, kScreenWidth, kScreenHeight},
                            .size = {static_cast<float>(width), static_cast<float>(height)},
                            .unused = {}};
        SDL_BindGPUGraphicsPipeline(pass, own_format ? panel_pipeline_own : panel_pipeline);
        SDL_PushGPUVertexUniformData(commands, 0, &placed, sizeof(placed));
        const SDL_GPUTextureSamplerBinding laid{.texture = overlay_texture, .sampler = sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &laid, 1);
        SDL_DrawGPUPrimitives(pass, kCorners, 1, 0, 0);
    }
    SDL_EndGPURenderPass(pass);
    const std::array<float, 4> screen_part{static_cast<float>(left), static_cast<float>(top), screen_width,
                                           screen_height};
    if (shown_look == Look::kXbr || shown_look == Look::kCrt) {
        show_xbr(commands, onto, width, height, screen_part, {grid[0], grid[1], fit, 0}, own, shown_look);
    }
    return {};
}

// The xbr look's last pass, or the crt look's (`which`): `crt_source` onto `onto` (of the same size), from the
// picture's pixels (`grid`: where the first starts, how wide each is); black around the screen's part `screen`.
void GpuRenderer::State::show_xbr(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* onto, int width, int height,
                                  const std::array<float, 4>& screen, const std::array<float, 4>& grid, bool own,
                                  Look which) {
    struct Placed {  // gpu_show.vert
        std::array<float, 4> dest;
        std::array<float, 4> source;
        std::array<float, 2> size;
        std::array<float, 2> unused;
    };
    struct Grid {  // gpu_xbr.frag, gpu_hyllian.frag
        std::array<float, 4> screen;
        std::array<float, 4> grid;
    };
    const auto wide = static_cast<float>(width);
    const auto high = static_cast<float>(height);
    const Placed placed{.dest = {0, 0, wide, high}, .source = {0, 0, wide, high}, .size = {wide, high}, .unused = {}};
    const Grid on{.screen = screen, .grid = grid};
    SDL_GPUColorTargetInfo colour{};
    colour.texture = onto;
    colour.clear_color = {.r = 0, .g = 0, .b = 0, .a = 1};
    colour.load_op = SDL_GPU_LOADOP_CLEAR;
    colour.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &colour, 1, nullptr);
    if (which == Look::kCrt) {
        SDL_BindGPUGraphicsPipeline(pass, own ? hyllian_pipeline_own : hyllian_pipeline);
    } else {
        SDL_BindGPUGraphicsPipeline(pass, own ? xbr_pipeline_own : xbr_pipeline);
    }
    SDL_PushGPUVertexUniformData(commands, 0, &placed, sizeof(placed));
    SDL_PushGPUFragmentUniformData(commands, 0, &on, sizeof(on));
    const SDL_GPUTextureSamplerBinding picture{.texture = crt_source, .sampler = sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &picture, 1);
    constexpr std::uint32_t kCorners = 6;
    SDL_DrawGPUPrimitives(pass, kCorners, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
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
    auto picture = s.make(commands, view, scene, shared);
    if (!picture) {
        SDL_CancelGPUCommandBuffer(commands);
        return std::unexpected{picture.error()};
    }
    if (auto drawn = s.render(commands, s.target, *picture, 1, nullptr, 0); !drawn) {
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
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(s.device);
    if (commands == nullptr) {
        return sdl_error("no command buffer");
    }
    auto picture = s.make(commands, view, scene, shared);
    if (!picture) {
        SDL_CancelGPUCommandBuffer(commands);
        return std::unexpected{picture.error()};
    }
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return sdl_error("the picture's pictures");
    }
    const std::int64_t number = s.kept_count++;
    const State::Kept& before = s.kept.at((static_cast<std::size_t>(number) + kKept - 1) % kKept);
    const State::Picture* last = before.number == number - 1 ? before.picture.get() : nullptr;
    std::shared_ptr<const State::Picture> keeping;
    if (last != nullptr && last->page == picture->page && last->quads.size() == picture->quads.size() &&
        last->moves_with == picture->moves_with && last->width == picture->width && last->height == picture->height &&
        last->in_playfield == picture->in_playfield && last->backdrop == picture->backdrop &&
        std::memcmp(last->quads.data(), picture->quads.data(), picture->quads.size() * sizeof(Quad)) == 0) {
        keeping = before.picture;
    } else {
        keeping = std::make_shared<const State::Picture>(std::move(*picture));
    }
    s.kept.at(static_cast<std::size_t>(number) % kKept) = {.number = number, .picture = std::move(keeping)};
    screen.kept = number;
    ++s.since.kept;
    return {};
}

std::expected<void, std::string> GpuRenderer::show(const Screen& screen, const Minimap* minimap, float between) {
    State& s = *state_;
    if (s.window == nullptr) {
        return std::unexpected{std::string{"no window"}};
    }
    const auto milliseconds = [](std::uint64_t from, std::uint64_t to) {
        return static_cast<double>(to - from) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    };
    const std::uint64_t began = SDL_GetPerformanceCounter();
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
    const std::uint64_t acquired = SDL_GetPerformanceCounter();
    s.times.remade = false;
    s.times.kept = s.since.kept;
    s.times.uploads = s.since.uploads;
    s.times.uploaded_bytes = s.since.uploaded_bytes;
    s.since = {};
    s.times.window_width = static_cast<int>(width);
    s.times.window_height = static_cast<int>(height);
    if (onto != nullptr) {  // none: the window is hidden; nothing to show on
        if (auto shown = s.show(commands, onto, static_cast<int>(width), static_cast<int>(height), screen, minimap,
                                false, between);
            !shown) {
            (void)SDL_SubmitGPUCommandBuffer(commands);
            return shown;
        }
    }
    const std::uint64_t recorded = SDL_GetPerformanceCounter();
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        s.in_work.reset();
        return sdl_error("the window's picture");
    }
    const std::uint64_t submitted = SDL_GetPerformanceCounter();
    s.times.waiting = milliseconds(began, acquired);
    s.times.recording = milliseconds(acquired, recorded);
    s.times.handing_over = milliseconds(recorded, submitted);
    return {};
}

std::expected<std::vector<std::uint8_t>, std::string> GpuRenderer::State::read_back(const Screen& screen, int width,
                                                                                    int height, const Minimap* minimap,
                                                                                    float between, int whole) {
    const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    SDL_GPUTexture* onto =
        texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, width, height);
    const SDL_GPUTransferBufferCreateInfo made{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = static_cast<Uint32>(bytes), .props = 0};
    SDL_GPUTransferBuffer* download_to = SDL_CreateGPUTransferBuffer(device, &made);
    const auto read = [&]() -> std::expected<std::vector<std::uint8_t>, std::string> {
        SDL_GPUCommandBuffer* commands =
            onto != nullptr && download_to != nullptr ? SDL_AcquireGPUCommandBuffer(device) : nullptr;
        if (commands == nullptr) {
            return sdl_error("no command buffer");
        }
        if (auto drawn = show(commands, onto, width, height, screen, minimap, true, between, whole); !drawn) {
            SDL_CancelGPUCommandBuffer(commands);
            in_work.reset();  // (what was drawn into it is gone with the commands)
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
        const SDL_GPUTextureTransferInfo to{.transfer_buffer = download_to,
                                            .offset = 0,
                                            .pixels_per_row = static_cast<Uint32>(width),
                                            .rows_per_layer = 0};
        SDL_DownloadFromGPUTexture(copy, &from, &to);
        SDL_EndGPUCopyPass(copy);
        SDL_GPUFence* done = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        if (done == nullptr) {
            in_work.reset();
            return sdl_error("the picture shown");
        }
        const bool waited = SDL_WaitForGPUFences(device, true, &done, 1);
        SDL_ReleaseGPUFence(device, done);
        const void* mapped = waited ? SDL_MapGPUTransferBuffer(device, download_to, false) : nullptr;
        if (mapped == nullptr) {
            return sdl_error("the picture shown, read back");
        }
        std::vector<std::uint8_t> pixels(bytes);
        std::memcpy(pixels.data(), mapped, bytes);
        SDL_UnmapGPUTransferBuffer(device, download_to);
        return pixels;
    };
    auto pixels = read();
    SDL_ReleaseGPUTransferBuffer(device, download_to);
    SDL_ReleaseGPUTexture(device, onto);
    return pixels;
}

std::expected<std::vector<std::uint8_t>, std::string> GpuRenderer::shown(const Screen& screen, int scale,
                                                                         const Minimap* minimap, float between) {
    return state_->read_back(screen, kScreenWidth * scale, kScreenHeight * scale, minimap, between, 0);
}

std::expected<std::vector<std::uint8_t>, std::string> GpuRenderer::shown_in(const Screen& screen, int width, int height,
                                                                            const Minimap* minimap) {
    return state_->read_back(screen, width, height, minimap, 0, 0);
}

void GpuRenderer::set_look(Look look) noexcept {
    state_->look = look;
}

void GpuRenderer::set_overlay(std::vector<std::uint8_t> pixels) {
    state_->overlay = std::move(pixels);
    state_->overlay_sent = false;
}

GpuRenderer::Look GpuRenderer::look() const noexcept {
    return state_->look;
}

std::expected<GpuRenderer::Whole, std::string> GpuRenderer::shown_whole(const Screen& screen, int scale) {
    State& s = *state_;
    const State::Kept* found = screen.kept >= 0 ? &s.kept.at(static_cast<std::size_t>(screen.kept) % kKept) : nullptr;
    if (found == nullptr || found->number != screen.kept || found->picture == nullptr) {
        return std::unexpected{std::string{"no such picture kept"}};
    }
    const int covered = found->picture->height > kPlayfieldHeight ? kHudHeight : 0;
    Whole whole{
        .width = found->picture->width * scale, .height = (found->picture->height - covered) * scale, .pixels = {}};
    auto pixels = s.read_back(screen, whole.width, whole.height, nullptr, 0, scale);
    if (!pixels) {
        return std::unexpected{pixels.error()};
    }
    whole.pixels = std::move(*pixels);
    return whole;
}

}  // namespace lvr
