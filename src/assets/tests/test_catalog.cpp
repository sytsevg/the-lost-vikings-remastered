// Tests for src/assets/catalog.hpp: the tables of what DATA.DAT holds, checked against the real game files.
#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "assets/assets.hpp"
#include "assets/catalog.hpp"
#include "assets/data_dat.hpp"
#include "assets/graphics.hpp"
#include "assets/hash.hpp"
#include "assets/level.hpp"
#include "assets/sprites.hpp"

using namespace lvr::assets;

namespace {

// The user's own game files in gamedata/original/, opened the way the game opens them. [real-data] tests skip when
// the folder is missing.
const lvr::assets::GameFiles& game_or_skip() {
    if (!std::filesystem::exists(LVR_ORIGINAL_DIR)) {
        SKIP("no " LVR_ORIGINAL_DIR " (put your own copy of the game there)");
    }
    static const auto files = lvr::assets::GameFiles::open(LVR_ORIGINAL_DIR);
    if (!files) {
        FAIL(files.error());
    }
    return *files;
}

}  // namespace

// --- Sprite catalogue ---

TEST_CASE("sprite catalogue: sorted, unique, and every frame size is drawable") {
    const std::span<const SpriteInfo> catalog = kSpriteCatalog;
    REQUIRE_FALSE(catalog.empty());
    for (std::size_t i = 1; i < catalog.size(); ++i) {
        CHECK(catalog[i - 1].entry < catalog[i].entry);
    }
    for (const SpriteInfo& info : catalog) {
        INFO("entry " << info.entry);
        CHECK_FALSE(info.name.empty());
        if (info.format == SpriteFormat::kPacked32) {
            CHECK(info.preview_palette != kNoEntry);
            continue;
        }
        if (info.format == SpriteFormat::kTiled) {
            CHECK(info.preview_palette != kNoEntry);
            CHECK(info.frame_runs().size() == 1);
            CHECK(info.runs[0].height % 8 == 0);
        }
        for (const FrameRun& run : info.frame_runs()) {
            CHECK(run.count > 0);
            CHECK(run.width % 4 == 0);
            if (info.format == SpriteFormat::kUnpacked) {
                // The game draws unpacked sprites as 8x8, 16x16, or 32 pixels wide by any height.
                const bool drawable = (run.width == 8 && run.height == 8) || (run.width == 16 && run.height == 16) ||
                                      (run.width == 32 && run.height > 0);
                CHECK(drawable);
            }
        }
    }
}

// --- Real data: sprites ---

namespace {

const SpriteInfo& catalogued(std::uint16_t entry) {
    const std::span<const SpriteInfo> catalog = kSpriteCatalog;
    const auto found = std::ranges::find(catalog, entry, &SpriteInfo::entry);
    REQUIRE(found != catalog.end());
    return *found;
}

std::uint64_t frames_hash(const std::vector<SpriteFrame>& frames) {
    lvr::assets::Fnv1a64 hash;
    for (const SpriteFrame& frame : frames) {
        hash.update(std::as_bytes(std::span{frame.image.pixels}));
    }
    return hash.value();
}

// Unpacked: counts bytes where "masked out" and "stored as 0" disagree. Zero means masked-out pixels are stored as
// 0 and drawn pixels never are, so colour 0 can stand for "transparent".
std::size_t unpacked_mask_mismatches(std::span<const std::byte> data) {
    std::size_t mismatches = 0;
    for (std::size_t group = 0; group + 9 <= data.size(); group += 9) {
        const auto mask = std::to_integer<unsigned>(data[group]);
        for (std::size_t bit = 0; bit < 8; ++bit) {
            const bool drawn = (mask & (0x80U >> bit)) != 0;
            mismatches += static_cast<std::size_t>(drawn != (data[group + 1 + bit] != std::byte{0}));
        }
    }
    return mismatches;
}

// Tiled: the same check per 72-byte tile, whose 8 mask bytes come before its 64 pixel bytes.
std::size_t tiled_mask_mismatches(std::span<const std::byte> data) {
    std::size_t mismatches = 0;
    for (std::size_t tile = 0; tile + kMaskedTileBytes <= data.size(); tile += kMaskedTileBytes) {
        for (std::size_t i = 0; i < 64; ++i) {
            const bool drawn = (std::to_integer<unsigned>(data[tile + (i / 8)]) & (0x80U >> (i % 8))) != 0;
            mismatches += static_cast<std::size_t>(drawn != (data[tile + 8 + i] != std::byte{0}));
        }
    }
    return mismatches;
}

// Packed32: counts drawn pixels with value 0 in one frame.
std::size_t packed_zero_pixels(std::span<const std::byte> frame) {
    std::size_t zeros = 0;
    std::size_t pos = 0;
    for (std::size_t row = 0; row < 4 * 32; ++row) {
        const auto mask = std::to_integer<unsigned>(frame[pos++]);
        const auto count = static_cast<std::size_t>(std::popcount(mask));
        for (std::size_t i = 0; i < count; ++i) {
            const auto pair = std::to_integer<unsigned>(frame[pos + (i / 2)]);
            zeros += static_cast<std::size_t>(((i % 2 == 0) ? (pair >> 4U) : (pair & 0x0fU)) == 0);
        }
        pos += (count + 1) / 2;
    }
    return zeros;
}

}  // namespace

TEST_CASE("real data: every catalogued sprite entry decodes exactly", "[real-data]") {
    const Archive* const archive = &game_or_skip().archive();

    std::size_t frame_count = 0;
    std::size_t tails = 0;
    for (const SpriteInfo& info : kSpriteCatalog) {
        INFO("entry " << info.entry << " " << info.name);
        const auto entry = decode_entry(*archive, info.entry);
        REQUIRE(entry.has_value());
        const auto frames = decode_sprite(*entry, info);
        REQUIRE(frames.has_value());
        REQUIRE_FALSE(frames->empty());
        frame_count += frames->size();

        if (info.format == SpriteFormat::kUnpacked) {
            CHECK(unpacked_mask_mismatches(*entry) == 0);
        }
        if (info.format == SpriteFormat::kTiled) {
            CHECK(tiled_mask_mismatches(*entry) == 0);
            CHECK(decode_entry(*archive, info.preview_palette)->size() == 48);  // 16 colours
        }
        if (info.format != SpriteFormat::kPacked32) {
            continue;  // raw, unpacked and tiled entries are used up exactly, or decoding fails
        }
        // Packed32: the table is followed directly by the frames, back to back. After the last frame come either
        // nothing or 640 bytes of 0xff (the size of the largest possible frame: 4 x 32 x (1 + 4)).
        CHECK(frames->front().offset == frames->size() * 2);
        for (std::size_t i = 0; i + 1 < frames->size(); ++i) {
            CHECK((*frames)[i].offset + (*frames)[i].size == (*frames)[i + 1].offset);
        }
        std::size_t zeros = 0;
        for (const SpriteFrame& frame : *frames) {
            zeros += packed_zero_pixels(std::span{*entry}.subspan(frame.offset, frame.size));
        }
        CHECK(zeros == 0);
        const auto tail = std::span{*entry}.subspan(frames->back().offset + frames->back().size);
        const bool ff_tail =
            tail.size() == 640 && std::ranges::all_of(tail, [](std::byte b) { return b == std::byte{0xff}; });
        CHECK((tail.empty() || ff_tail));
        tails += static_cast<std::size_t>(ff_tail);
        CHECK(decode_entry(*archive, info.preview_palette)->size() == 48);  // 16 colours
    }
    CHECK(kSpriteCatalog.size() == 161);
    CHECK(frame_count == 1274);
    CHECK(tails == 44);
}

TEST_CASE("real data: sprite pixels match an independent Python decoder", "[real-data]") {
    const Archive* const archive = &game_or_skip().archive();

    // FNV-1a 64 over every frame's pixels in order (0 = transparent), from the prototype decoder.
    const struct {
        std::uint16_t entry;
        std::size_t frames;
        std::uint64_t hash;
    } expected[] = {
        {2, 80, 0x997a980999105527ULL},    // unpacked 8x8: font
        {14, 24, 0xc5df308bdf8df839ULL},   // raw 16x16: HUD items
        {221, 53, 0xe9fe32101223c8dcULL},  // packed32: Baleog
        {303, 13, 0xa5fa26e5701daf36ULL},  // unpacked, mixed: 12 items at 16x16, then a 32x32 steak
        {327, 6, 0x653660a87fc20584ULL},   // unpacked, mixed: magnet 32x32, sparks 8x8
        {368, 4, 0x9e7867a6360be3a0ULL},   // unpacked 32x176: lightning
        {403, 20, 0x352e310479fa13f3ULL},  // packed32 without the 0xff tail
        {293, 4, 0x55b32766832a8e76ULL},   // tiled: a 128x32 sheet of four 32x32 frames
        {342, 2, 0x56194d564c139e4bULL},   // tiled: a 32x16 sheet of two 16x16 frames (the target)
    };
    for (const auto& e : expected) {
        INFO("entry " << e.entry);
        const auto frames = decode_sprite(*decode_entry(*archive, e.entry), catalogued(e.entry));
        REQUIRE(frames.has_value());
        CHECK(frames->size() == e.frames);
        CHECK(frames_hash(*frames) == e.hash);
    }
}

// --- Tables against the level and scene headers ---

TEST_CASE("real data: the sprite catalogue covers every sprite entry a header loads", "[real-data]") {
    const Archive* const archive = &game_or_skip().archive();

    std::set<std::uint16_t> loaded;
    std::size_t overlaps = 0;
    const auto scan = [&](std::uint16_t header_entry) {
        const auto header = parse_level_header(*decode_entry(*archive, header_entry));
        REQUIRE(header.has_value());
        for (const SpriteSetRef& set : header->sprite_sets) {
            loaded.insert(set.entry);
        }
        for (const PackedSetRef& set : header->packed_sets) {
            loaded.insert(set.entry);
        }
        // Our reading of the sprite-set fields: [slot, slot + sheet columns) don't overlap within a header, except
        // where a level swaps in a few frames (level 20: tools over items; level 25: also 271 over 302).
        const auto& sets = header->sprite_sets;
        for (std::size_t i = 0; i < sets.size(); ++i) {
            for (std::size_t j = i + 1; j < sets.size(); ++j) {
                const auto end = [](const SpriteSetRef& s) { return s.slot + (s.sheet & 0xffU); };
                overlaps += static_cast<std::size_t>(sets[i].slot < end(sets[j]) && sets[j].slot < end(sets[i]));
            }
        }
    };
    for (const LevelInfo& info : kLevels) {
        scan(info.header_entry);
    }
    for (const SceneInfo& info : kScenes) {
        scan(info.header_entry);
    }
    CHECK(overlaps == 4);

    std::set<std::uint16_t> catalogued;
    for (const SpriteInfo& info : kSpriteCatalog) {
        catalogued.insert(info.entry);
    }
    std::vector<std::uint16_t> missing;
    std::ranges::set_difference(loaded, catalogued, std::back_inserter(missing));
    CHECK(missing.empty());
    // Catalogued but loaded by no header: the HUD graphics (loaded by VIKINGS.EXE itself) and five unused sets.
    std::vector<std::uint16_t> extra;
    std::ranges::set_difference(catalogued, loaded, std::back_inserter(extra));
    CHECK(extra == std::vector<std::uint16_t>{2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 240, 293, 341, 342, 348});
}

TEST_CASE("real data: every DATA.DAT entry is accounted for", "[real-data]") {
    // docs/formats/unknowns.md lists the entries that nothing below accounts for. If this fails because one of them
    // got decoded (or a table changed), update that page and this list together.
    const Archive* const archive = &game_or_skip().archive();

    std::set<std::size_t> known{kConfigEntry, 382, kSceneObjectDbEntry, kIntroInputEntry, kEmptyInputEntry};
    // (382: an identical copy of the title header.) Sound: drivers and instrument banks; the music and the sound
    // effects are XMIDI and counted below.
    for (std::size_t d = 0; d < kDriverCount; ++d) {
        known.insert(kFirstDriverEntry + d);
    }
    for (std::size_t v = 0; v < kDeviceVariantCount; ++v) {
        known.insert(kFirstInstrumentBankEntry + v);
    }
    for (const DemoInfo& demo : kDemos) {
        known.insert(demo.input_entry);
    }
    const auto add_header = [&](std::uint16_t header_entry) {
        known.insert(header_entry);
        const auto header = parse_level_header(*decode_entry(*archive, header_entry));
        REQUIRE(header.has_value());
        known.insert(header->map_entry);  // a tilemap, or the full-screen image of an image scene
        if (header->is_tiled()) {
            known.insert({header->tileset_entry, header->tileset_entry + 1U, header->metatiles_entry});
        }
        for (const PaletteRef& ref : header->palettes) {
            known.insert(ref.entry);
        }
        for (const SpriteSetRef& set : header->sprite_sets) {
            known.insert(set.entry);
        }
        for (const PackedSetRef& set : header->packed_sets) {
            known.insert(set.entry);
        }
    };
    for (const LevelInfo& info : kLevels) {
        add_header(info.header_entry);
        known.insert(info.object_db_entry);
    }
    for (const SceneInfo& info : kScenes) {
        add_header(info.header_entry);
        if (info.hud_entry != kNoEntry) {
            known.insert(info.hud_entry);
        }
    }
    for (const ImageInfo& info : kImages) {
        known.insert({info.image_entry, info.palette_entry});
    }
    for (const SpriteInfo& info : kSpriteCatalog) {
        known.insert(info.entry);
    }
    std::vector<std::size_t> leftovers;
    for (std::size_t i = 0; i < archive->entry_count(); ++i) {
        const auto entry = decode_entry(*archive, i);
        REQUIRE(entry.has_value());
        if (!known.contains(i) && detect_kind(i, *entry) != EntryKind::kXmidi) {
            leftovers.push_back(i);
        }
    }
    // VIKINGS.EXE never loads these (docs/formats/unknowns.md).
    CHECK(leftovers == std::vector<std::size_t>{173, 241, 534});
}

TEST_CASE("sound and demo tables are consistent") {
    for (const SoundDevice& device : kSoundDevices) {
        CHECK(device.driver < kDriverCount);
        CHECK(device.variant < kDeviceVariantCount);
    }
    for (std::size_t t = 1; t < kTunes.size(); ++t) {
        CHECK(kTunes[t] != kTunes[t - 1]);
    }
    for (const DemoInfo& demo : kDemos) {
        CHECK(std::ranges::any_of(kLevels, [&](const LevelInfo& level) { return level.number == demo.level; }));
    }
}

TEST_CASE("real data: the sound and demo entries VIKINGS.EXE computes are what we think", "[real-data]") {
    const Archive* const archive = &game_or_skip().archive();
    const auto kind = [&](std::size_t entry) { return detect_kind(entry, *decode_entry(*archive, entry)); };

    // Every tune exists in all five device versions, and so do the sound effects.
    for (const std::uint16_t first : kTunes) {
        for (std::size_t v = 0; v < kDeviceVariantCount; ++v) {
            INFO("tune entry " << first + v);
            CHECK(kind(first + v) == EntryKind::kXmidi);
        }
    }
    for (std::size_t v = 0; v < kDeviceVariantCount; ++v) {
        CHECK(kind(kFirstSoundEffectsEntry + v) == EntryKind::kXmidi);
    }
    CHECK(kind(kMt32InstrumentUploadEntry) == EntryKind::kXmidi);

    // Instrument banks: 524 and 528 (PC speaker, General MIDI) are empty XMIDI placeholders; 525 = 526 (OPL) and 527
    // (MT-32) are timbre libraries.
    CHECK(kind(kFirstInstrumentBankEntry) == EntryKind::kXmidi);
    CHECK(kind(kFirstInstrumentBankEntry + 4) == EntryKind::kXmidi);
    CHECK(*decode_entry(*archive, kFirstInstrumentBankEntry + 1) ==
          *decode_entry(*archive, kFirstInstrumentBankEntry + 2));

    // Every driver is a Miles Design AIL driver.
    constexpr std::string_view kCopyright = "Copyright (C) 1991,1992 Miles Design, Inc.";
    for (std::size_t d = 0; d < kDriverCount; ++d) {
        INFO("driver entry " << kFirstDriverEntry + d);
        const auto driver = *decode_entry(*archive, kFirstDriverEntry + d);
        REQUIRE(driver.size() > 2 + kCopyright.size());
        CHECK(std::ranges::equal(std::span(driver).subspan(2, kCopyright.size()), kCopyright,
                                 [](std::byte b, char c) { return std::to_integer<char>(b) == c; }));
    }

    // Input recordings: (input word, ticks) pairs ending with the pause bit for 0xffff ticks.
    std::vector<std::uint16_t> recordings{kIntroInputEntry, kEmptyInputEntry};
    for (const DemoInfo& demo : kDemos) {
        recordings.push_back(demo.input_entry);
    }
    for (const std::uint16_t entry : recordings) {
        INFO("recording entry " << entry);
        const auto data = *decode_entry(*archive, entry);
        REQUIRE(data.size() % 4 == 0);
        REQUIRE(data.size() >= 4);
        const auto word = [&](std::size_t i) {
            return static_cast<std::uint16_t>(std::to_integer<unsigned>(data[i]) |
                                              (std::to_integer<unsigned>(data[i + 1]) << 8U));
        };
        CHECK(word(data.size() - 4) == 0x1000);
        CHECK(word(data.size() - 2) == 0xffff);
    }
}

TEST_CASE("real data: every scene decodes", "[real-data]") {
    const Archive* const archive = &game_or_skip().archive();

    // The title screen has two identical headers.
    CHECK(*decode_entry(*archive, 381) == *decode_entry(*archive, 382));

    for (const SceneInfo& info : kScenes) {
        INFO("scene " << info.name);
        const auto header = parse_level_header(*decode_entry(*archive, info.header_entry));
        REQUIRE(header.has_value());
        for (const PaletteRef& ref : header->palettes) {
            Palette palette{};
            CHECK(load_palette(palette, *decode_entry(*archive, ref.entry), ref.first_color).has_value());
        }
        if (header->is_tiled()) {
            const auto map = decode_tilemap(*decode_entry(*archive, header->map_entry), header->width, header->height);
            const auto tileset = decode_tileset(*decode_entry(*archive, header->tileset_entry));
            const auto metatiles = decode_metatiles(*decode_entry(*archive, header->metatiles_entry));
            REQUIRE(map.has_value());
            CHECK(render_tilemap(*map, *metatiles, *tileset).has_value());
        } else {
            const auto top = decode_screen_image(*decode_entry(*archive, header->map_entry));
            REQUIRE(top.has_value());
            CHECK(top->height == 176);
            REQUIRE(info.hud_entry != kNoEntry);
            const auto hud = decode_screen_image(*decode_entry(*archive, info.hud_entry));
            REQUIRE(hud.has_value());
            CHECK(hud->height == 64);  // 176 + 64 = a full 240-line screen
        }
    }
    for (const ImageInfo& info : kImages) {
        INFO("image " << info.name);
        const auto image = decode_screen_image(*decode_entry(*archive, info.image_entry));
        REQUIRE(image.has_value());
        CHECK(image->height == 64);  // both are HUD-area images
        // The palette (loaded at colour 0) covers every colour the image uses.
        const std::size_t colours = decode_entry(*archive, info.palette_entry)->size() / 3;
        CHECK(std::ranges::max(image->pixels) < colours);
    }
}

TEST_CASE("real data: the HUD panel (entry 1) is drawn with the HUD palette at colour 0", "[real-data]") {
    const Archive* const archive = &game_or_skip().archive();

    const auto panel = decode_screen_image(*decode_entry(*archive, 1));
    REQUIRE(panel.has_value());
    CHECK(panel->width == kScreenWidth);
    CHECK(panel->height == 64);
    // Reference value from an independent Python implementation.
    CHECK(lvr::assets::fnv1a64(std::as_bytes(std::span{panel->pixels})) == 0x7e693ab0a32542ceULL);
    const std::set<std::uint8_t> used(panel->pixels.begin(), panel->pixels.end());
    CHECK(used == std::set<std::uint8_t>{1, 2, 4, 9, 10, 12, 14});

    // Colours 0-15 come from entry 3 in every level: it is loaded at colour 0 and no later palette overwrites them.
    constexpr std::uint16_t kHudPalette = 3;
    CHECK(decode_entry(*archive, kHudPalette)->size() == 16 * 3);
    for (const LevelInfo& info : kLevels) {
        INFO("level " << int{info.number});
        const auto header = parse_level_header(*decode_entry(*archive, info.header_entry));
        REQUIRE(header.has_value());
        const auto& palettes = header->palettes;
        const auto last_hud = std::ranges::find_last_if(
            palettes, [](const PaletteRef& ref) { return ref.entry == kHudPalette && ref.first_color == 0; });
        REQUIRE(!last_hud.empty());
        CHECK(std::ranges::none_of(std::next(last_hud.begin()), palettes.end(),
                                   [](const PaletteRef& ref) { return ref.first_color < 16; }));
    }
}
