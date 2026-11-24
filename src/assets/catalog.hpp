#pragma once

// What this game's DATA.DAT contains, as far as we know it (docs/formats/data-dat.md#entry-map): the tables that
// tie entry numbers to meaning. The level, sound and demo tables are hardcoded in VIKINGS.EXE / SETUP.EXE
// (docs/original/game-flow.md, docs/formats/audio.md); the scene, image and sprite tables are ours, found by decoding
// and looking. The generic format decoders are in data_dat.*, graphics.*, sprites.* and
// level.*; this file only says which entry is what.

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "assets/data_dat.hpp"
#include "assets/reader.hpp"
#include "assets/sprites.hpp"

namespace lvr::assets {

// --- Level table ---

struct LevelInfo {
    std::uint8_t number = 0;  // 1..37, the order the game plays them in
    std::string_view world;
    std::uint16_t header_entry = 0;     // DATA.DAT entry with the level header
    std::uint16_t object_db_entry = 0;  // DATA.DAT entry with the world's object database (one per world)
};

// Hardcoded in VIKINGS.EXE (from RyanMallon's liblv, verified against the reference release).
inline constexpr std::array<LevelInfo, 37> kLevels{{
    {1, "spaceship", 198, 449},
    {2, "spaceship", 200, 449},
    {3, "spaceship", 202, 449},
    {4, "spaceship", 204, 449},
    {5, "prehistoric", 40, 450},
    {6, "prehistoric", 42, 450},
    {7, "prehistoric", 44, 450},
    {8, "prehistoric", 46, 450},
    {9, "prehistoric", 48, 450},
    {10, "prehistoric", 50, 450},
    {11, "prehistoric", 52, 450},
    {12, "egypt", 75, 451},
    {13, "egypt", 77, 451},
    {14, "egypt", 79, 451},
    {15, "egypt", 81, 451},
    {16, "egypt", 83, 451},
    {17, "egypt", 85, 451},
    {18, "factory", 114, 452},
    {19, "factory", 116, 452},
    {20, "factory", 118, 452},
    {21, "factory", 120, 452},
    {22, "factory", 122, 452},
    {23, "factory", 124, 452},
    {24, "factory", 126, 452},
    {25, "factory", 128, 452},
    {26, "wacky", 156, 453},
    {27, "wacky", 158, 453},
    {28, "wacky", 160, 453},
    {29, "wacky", 162, 453},
    {30, "wacky", 164, 453},
    {31, "wacky", 166, 453},
    {32, "wacky", 168, 453},
    {33, "wacky", 170, 453},
    {34, "spaceship-return", 206, 449},
    {35, "spaceship-return", 208, 449},
    {36, "spaceship-return", 210, 449},
    {37, "spaceship-return", 212, 449},
}};

// --- Scene table ---

// Non-playable screens: logos, title, story scenes. They use the level header format too. A *tiled* scene is drawn
// like a level; an *image* scene's header has no tileset and its "map" entry is a full-screen image
// (decode_screen_image). The HUD-area image below an image scene isn't in its header: VIKINGS.EXE picks it from the
// header flags (0x02: 384, 0x40: 531; docs/original/game-flow.md#header-flags-offset-0x1c-read-by-the-loader).
struct SceneInfo {
    std::string_view name;
    std::uint16_t header_entry = kNoEntry;
    std::uint16_t hud_entry = kNoEntry;
    std::string_view description;
};

inline constexpr std::array<SceneInfo, 10> kScenes{{
    {"interplay", 390, kNoEntry, "Interplay logo"},
    {"silicon-synapse", 396, kNoEntry, "Silicon & Synapse logo"},
    {"title", 381, 384, "title screen (header 382 is an identical copy)"},
    {"home-intro", 431, kNoEntry, "the Vikings' village, intro"},
    {"home-demo", 432, kNoEntry, "the Vikings' village, demo"},
    {"home-ending", 434, kNoEntry, "the Vikings' village, ending"},
    {"galaxy", 402, kNoEntry, "galaxy, shown when travelling between worlds"},
    {"starfield", 433, 531, "starfield"},
    {"ship", 218, kNoEntry, "Viking ship with speakers, ending"},
    {"game-over", 369, kNoEntry, "game over: the Vikings fall on a moonlit sea"},
}};

// Stand-alone full-screen images not tied to a scene header (yet).
struct ImageInfo {
    std::string_view name;
    std::uint16_t image_entry = kNoEntry;
    std::uint16_t palette_entry = kNoEntry;  // loaded at colour 0; covers every colour the image uses
    std::string_view description;
};

inline constexpr std::array<ImageInfo, 2> kImages{{
    // VIKINGS.EXE loads it itself (no header names it). It uses colours 1-14 only, and every level puts the HUD
    // palette (entry 3) at colour 0 with nothing after it overwriting colours 0-15, so it looks the same everywhere.
    {"hud-panel", 1, 3, "HUD panel below the playfield: stone frame, three Viking slots with health dots, trash can"},
    // Shown in the HUD area of the ship scene (header 218, flag 0x20), so with the ship's palette: its colours
    // (48-105) all come from the ship's first palette, 214 at colour 0.
    {"hud-logo", 529, 214, "logo in the HUD area of the ship scene"},
}};

// --- Fixed entries VIKINGS.EXE loads itself ---

// The object database of all scenes (slots 38-47; the game-over scene has none). VIKINGS.EXE loads it at start-up.
inline constexpr std::uint16_t kSceneObjectDbEntry = 454;

// --- Sound (docs/formats/audio.md#sound-devices) ---

// The configuration (entry 0) holds a driver index d and a device variant v (0-4). The game loads driver
// kFirstDriverEntry + d, sound effects kFirstSoundEffectsEntry + v, instrument bank kFirstInstrumentBankEntry + v,
// and tune t as kTunes[t] + v.
inline constexpr std::uint16_t kFirstDriverEntry = 455;
inline constexpr std::uint16_t kFirstSoundEffectsEntry = 519;
inline constexpr std::uint16_t kFirstInstrumentBankEntry = 524;
inline constexpr std::uint16_t kMt32InstrumentUploadEntry = 533;  // played once at start-up on an MT-32
inline constexpr std::size_t kDriverCount = 9;
inline constexpr std::size_t kDeviceVariantCount = 5;

// Tune number (level header byte 0x05) -> first of its five entries (VIKINGS.EXE ds:A384).
inline constexpr std::array<std::uint16_t, 11> kTunes{494, 489, 464, 469, 474, 479, 499, 484, 504, 509, 514};

// The choices SETUP.EXE offers, and what it writes for each.
struct SoundDevice {
    std::string_view name;
    std::uint8_t driver = 0;   // driver index d
    std::uint8_t variant = 0;  // device variant v
    bool sound = true;         // false: "No Sound" (config words 1 and 2 = 0x8000)
};

inline constexpr std::array<SoundDevice, 10> kSoundDevices{{
    {"No Sound", 0, 0, false},
    {"PC Speaker", 0, 0},
    {"Adlib", 1, 1},
    {"Adlib Gold", 2, 2},
    {"Sound Blaster", 3, 1},
    {"Sound Blaster Pro", 4, 2},
    {"Pro Audio Spectrum", 6, 1},
    {"Pro Audio Spectrum 16", 7, 2},
    {"Roland MT-32", 8, 3},
    {"Roland SCC-1/General Midi", 8, 4},
}};

// --- Demo input recordings (docs/original/input.md#demo-recordings) ---

// Attract-mode demos, played in this order (VIKINGS.EXE ds:2B66 and ds:2B74).
struct DemoInfo {
    std::uint8_t level = 0;  // LevelInfo::number
    std::uint16_t input_entry = 0;
};

inline constexpr std::array<DemoInfo, 6> kDemos{{{3, 418}, {10, 419}, {9, 420}, {16, 422}, {24, 421}, {26, 448}}};
inline constexpr std::uint16_t kIntroInputEntry = 443;  // drives the village intro (slot 43)
inline constexpr std::uint16_t kEmptyInputEntry = 444;  // slots 44-47: no input

// --- Sprite catalogue ---

// A DATA.DAT entry that holds sprites. The names are our own labels.
struct SpriteInfo {
    std::uint16_t entry = kNoEntry;
    SpriteFormat format = SpriteFormat::kUnpacked;
    std::string_view name;
    std::array<FrameRun, 2> runs{};  // raw and unpacked: up to two; tiled: one; unused runs have count 0
    // Packed32 and tiled: a 16-colour palette entry for previews. The game picks the colours when it draws.
    std::uint16_t preview_palette = kNoEntry;

    std::span<const FrameRun> frame_runs() const noexcept { return std::span{runs}.first(runs[1].count != 0 ? 2 : 1); }
};

// Row builders for kSpriteCatalog. consteval: they only exist at compile time.
namespace catalog_detail {

consteval SpriteInfo raw(std::uint16_t entry, std::string_view name, FrameRun run) {
    return SpriteInfo{.entry = entry, .format = SpriteFormat::kRaw, .name = name, .runs = {run, FrameRun{}}};
}

consteval SpriteInfo unpacked(std::uint16_t entry, std::string_view name, FrameRun run, FrameRun second = {}) {
    return SpriteInfo{.entry = entry, .format = SpriteFormat::kUnpacked, .name = name, .runs = {run, second}};
}

consteval SpriteInfo packed(std::uint16_t entry, std::string_view name, std::uint16_t preview_palette) {
    return SpriteInfo{
        .entry = entry, .format = SpriteFormat::kPacked32, .name = name, .preview_palette = preview_palette};
}

consteval SpriteInfo tiled(std::uint16_t entry, std::string_view name, FrameRun run, std::uint16_t preview_palette) {
    return SpriteInfo{.entry = entry,
                      .format = SpriteFormat::kTiled,
                      .name = name,
                      .runs = {run, FrameRun{}},
                      .preview_palette = preview_palette};
}

}  // namespace catalog_detail

// Every sprite entry: all entries that a level or scene header loads, plus the HUD graphics and five sets no
// header loads (240, 293, 341, 342, 348). Sorted by entry.
// Frame runs found by decoding every entry with each possible frame size and looking at the results
// (docs/formats/graphics.md#sprite-catalogue). Names mostly follow Albert Thalheim's list in RyanMallon's tools.
// Packed32 and tiled preview palettes come from that list where it names one, otherwise picked by eye.
// (Built in a lambda only so the rows can use the short builder names from catalog_detail.)
inline constexpr auto kSpriteCatalog = [] {
    using namespace catalog_detail;
    return std::array{
        unpacked(2, "font", {8, 8, 80}),
        raw(4, "erik-portrait", {32, 24, 1}),
        raw(5, "baleog-portrait", {32, 24, 1}),
        raw(6, "olaf-portrait", {32, 24, 1}),
        raw(7, "erik-portrait-inactive", {32, 24, 1}),
        raw(8, "baleog-portrait-inactive", {32, 24, 1}),
        raw(9, "olaf-portrait-inactive", {32, 24, 1}),
        raw(10, "erik-portrait-dead", {32, 24, 1}),
        raw(11, "baleog-portrait-dead", {32, 24, 1}),
        raw(12, "olaf-portrait-dead", {32, 24, 1}),
        raw(13, "health", {32, 7, 7}),
        raw(14, "items", {16, 16, 24}),
        packed(221, "baleog", 220),
        unpacked(222, "arrow", {8, 8, 8}),
        unpacked(223, "fire-arrow", {8, 8, 8}),
        packed(224, "erik", 229),
        packed(225, "olaf", 220),
        packed(226, "erik-2", 229),
        packed(227, "baleog-2", 220),
        packed(228, "olaf-2", 220),
        unpacked(230, "drawbridge-wheel", {16, 16, 1}),
        unpacked(231, "spike-elevator", {32, 32, 1}),
        unpacked(232, "laser", {8, 8, 12}),
        unpacked(233, "gun-turret", {32, 32, 2}),
        unpacked(234, "turret-bullet", {16, 16, 4}),
        packed(235, "janitor-alien", 229),
        packed(236, "robot", 229),
        unpacked(237, "question-box", {16, 16, 1}),
        packed(239, "exit-galaxy", 238),
        unpacked(240, "viking-band", {32, 32, 12}),
        packed(242, "pharaoh-small", 241),
        unpacked(243, "the-end", {32, 32, 3}),
        unpacked(244, "setting-sun", {16, 16, 4}),
        unpacked(245, "exploding-star", {32, 32, 2}),
        packed(248, "dinosaur", 246),
        packed(250, "caveman", 249),
        packed(252, "snail", 251),
        unpacked(253, "snail-spit", {8, 8, 8}),
        packed(255, "scorpion", 254),
        packed(257, "flower", 256),
        packed(259, "mummy", 258),
        packed(261, "scarf-slug", 260),
        unpacked(262, "slug-spit", {8, 8, 4}),
        packed(264, "pharaoh", 263),
        packed(266, "snake", 265),
        packed(268, "rolling-blades", 267),
        packed(270, "gun-tractor", 269),
        unpacked(271, "tractor-bullet", {8, 8, 4}),
        unpacked(272, "striped-gun", {32, 32, 2}),
        packed(274, "bomber-robot", 273),
        unpacked(275, "bomb-explosion", {8, 8, 2}),
        packed(277, "brain", 276),
        unpacked(278, "sparkles", {8, 8, 6}),
        packed(281, "slug", 279),
        packed(284, "cyborg-runner", 282),
        packed(285, "rip", 282),
        unpacked(286, "hazard-door", {16, 16, 4}),
        unpacked(287, "brown-door", {16, 16, 15}),
        unpacked(288, "bubble-small", {8, 8, 3}),
        unpacked(289, "bubble-medium", {16, 16, 3}),
        unpacked(290, "bubble-large", {32, 32, 3}),
        tiled(293, "lavafall", {32, 32, 4}, 292),  // no header loads it; name and palette are our guesses
        unpacked(294, "lava", {32, 32, 2}),
        unpacked(295, "fireball", {16, 16, 6}),
        unpacked(296, "falling-stone", {32, 32, 1}),
        unpacked(297, "star-burst", {16, 16, 2}),
        unpacked(298, "steel-block", {32, 32, 1}),
        packed(299, "bubble-creature", 292),
        unpacked(300, "switch", {16, 16, 2}),
        unpacked(301, "button", {16, 16, 2}),
        unpacked(302, "keyholes", {16, 16, 3}),
        unpacked(303, "items", {16, 16, 12}, {32, 32, 1}),
        unpacked(304, "tools", {16, 16, 1}),
        unpacked(305, "lab-tools", {16, 16, 4}),
        unpacked(306, "erik-frame", {16, 16, 2}),
        unpacked(307, "viking-wives", {32, 32, 2}),
        unpacked(308, "padlocks", {16, 16, 3}),
        unpacked(309, "tools-small", {8, 8, 3}),
        packed(310, "hieroglyph-monster", 315),
        unpacked(311, "glyph-blocks", {16, 16, 2}),
        unpacked(312, "hieroglyphs", {16, 16, 4}),
        unpacked(313, "coconuts", {16, 16, 4}),
        unpacked(314, "gold-drop", {16, 16, 5}),
        unpacked(316, "stone-block", {32, 32, 1}),
        unpacked(317, "stone-wedge", {16, 16, 1}),
        unpacked(318, "crumbling-block", {16, 16, 4}),
        unpacked(319, "piston", {16, 16, 2}),
        unpacked(320, "spikes", {32, 32, 1}),
        unpacked(321, "steel-truss", {16, 16, 3}),
        packed(322, "bomb", 291),
        packed(323, "old-man", 229),
        unpacked(324, "elevator", {32, 32, 1}),
        unpacked(325, "crusher", {32, 32, 3}),
        unpacked(326, "hazard-block", {16, 16, 2}),
        unpacked(327, "magnet", {32, 32, 2}, {8, 8, 4}),
        unpacked(328, "seesaw", {32, 32, 2}),
        unpacked(329, "spring", {32, 32, 2}),
        unpacked(330, "riveted-block", {32, 32, 1}),
        unpacked(331, "pneumatic-pump", {32, 32, 2}),
        unpacked(332, "pump-parts", {16, 16, 4}),
        unpacked(333, "girder", {16, 16, 3}),
        unpacked(334, "cable", {16, 16, 3}),
        unpacked(335, "vertical-spike", {8, 8, 3}),
        unpacked(336, "steel-blocks", {16, 16, 3}),
        unpacked(337, "lava-drop", {16, 16, 6}),
        unpacked(338, "crumbling-wall", {16, 16, 4}),
        unpacked(339, "bridge", {16, 16, 3}),
        unpacked(340, "hazard-press", {32, 32, 1}),
        unpacked(341, "orange-block", {32, 32, 1}),
        tiled(342, "target", {16, 16, 2}, 315),  // the bullseye; no header loads it, palette from Thalheim's list
        unpacked(343, "fan-horizontal", {32, 32, 4}),
        unpacked(344, "fan-wind-horizontal", {8, 8, 6}),
        unpacked(345, "fan-vertical", {32, 32, 4}),
        unpacked(346, "fan-wind-vertical", {8, 8, 6}),
        unpacked(347, "monster-elevator", {32, 32, 2}),
        unpacked(348, "fist-piston", {32, 32, 2}),
        unpacked(349, "punch", {16, 16, 4}),
        unpacked(350, "crumbling-wacky-tile", {16, 16, 4}),
        unpacked(351, "wacky-block", {32, 32, 1}),
        unpacked(352, "crumbling-checker-tile", {16, 16, 4}),
        unpacked(353, "eight-ball", {16, 16, 6}),
        packed(354, "tomator-top-left", 279),
        packed(355, "tomator-top-right", 279),
        packed(356, "tomator-bottom-left", 279),
        packed(357, "tomator-bottom-right", 279),
        packed(358, "tomator-effect", 291),
        packed(359, "double-switch", 291),
        unpacked(360, "hazard-panel", {32, 32, 2}),
        unpacked(362, "red-ball-burst", {8, 8, 2}),
        unpacked(368, "lightning", {32, 176, 4}),
        unpacked(371, "game-over-baleog", {32, 32, 6}),
        unpacked(372, "game-over-erik", {32, 32, 6}),
        unpacked(373, "game-over-olaf", {32, 32, 6}),
        packed(375, "boat-top-left", 374),
        packed(376, "boat-top-right", 374),
        packed(377, "boat-bottom-left", 374),
        packed(378, "boat-bottom-right", 374),
        packed(403, "galaxy-warp-1", 220),
        packed(404, "galaxy-warp-2", 220),
        packed(405, "galaxy-warp-3", 220),
        packed(406, "galaxy-warp-4", 220),
        packed(407, "galaxy-warp-5", 220),
        packed(408, "galaxy-warp-6", 220),
        packed(409, "galaxy-warp-7", 220),
        packed(410, "galaxy-warp-8", 220),
        packed(411, "galaxy-warp-9", 220),
        packed(412, "galaxy-warp-10", 220),
        unpacked(413, "menu-cursor", {16, 16, 1}),
        packed(414, "game-over-g", 383),
        packed(415, "game-over-ame", 383),
        packed(416, "game-over-o", 383),
        packed(417, "game-over-ver", 383),
        unpacked(428, "alien-ship", {32, 32, 8}, {8, 8, 4}),
        unpacked(435, "snore", {8, 8, 4}),
        unpacked(436, "moon", {32, 32, 4}),
        unpacked(437, "stars", {8, 8, 40}),
        unpacked(439, "spaceship", {32, 32, 3}),
        packed(441, "home-hug-1", 220),
        packed(442, "home-hug-2", 220),
        unpacked(445, "ship-flame", {16, 16, 3}),
        unpacked(447, "earth", {16, 16, 5}),
    };
}();
static_assert(std::ranges::is_sorted(kSpriteCatalog, {}, &SpriteInfo::entry));

// Decodes a catalogued sprite entry with the format and frame runs the catalogue gives it.
inline std::expected<std::vector<SpriteFrame>, Error> decode_sprite(std::span<const std::byte> data,
                                                                    const SpriteInfo& info) {
    return decode_sprite_frames(data, info.format, info.frame_runs());
}

}  // namespace lvr::assets
