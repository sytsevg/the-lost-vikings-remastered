// All libFuzzer targets for the decoders in src/assets (docs/decisions.md, ADR-0002). libFuzzer needs one entry point
// per binary, so each binary compiles this file with exactly one LVR_FUZZ_<TARGET> defined (see the fuzzers block in
// src/CMakeLists.txt). To add a target: add a block here and one lvr_add_fuzzer() line there.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "assets/data_dat.hpp"
#include "assets/graphics.hpp"
#include "assets/level.hpp"
#include "assets/script.hpp"
#include "assets/snes.hpp"
#include "assets/sprites.hpp"
#include "assets/text.hpp"

namespace {

[[maybe_unused]] std::span<const std::byte> as_input(const std::uint8_t* data, std::size_t size) {
    return std::as_bytes(std::span{data, size});
}

}  // namespace

// LZSS decompression on arbitrary input.
#if defined(LVR_FUZZ_LZSS)
// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto out = lvr::assets::lzss_decompress(as_input(data, size));
    if (out && out->consumed > size) {
        __builtin_trap();  // consumed must never exceed the input
    }
    return 0;
}
#endif

// A whole DATA.DAT: offset table, every entry decoded, kind detection, content hash. Also covers ByteReader.
#if defined(LVR_FUZZ_ARCHIVE)
// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto input = as_input(data, size);
    const auto archive = lvr::assets::Archive::parse(input);
    if (!archive) {
        return 0;
    }
    for (std::size_t i = 0; i < archive->entry_count(); ++i) {
        const auto decoded = lvr::assets::decode_entry(*archive, i);
        if (decoded) {
            (void)lvr::assets::detect_kind(i, *decoded);
        }
    }
    (void)lvr::assets::data_content_hash(input);
    return 0;
}
#endif

// Graphics decoders (palettes, planar images, tiles, maps, sprites) on arbitrary input. The first two bytes pick
// image dimensions so they vary too.
#if defined(LVR_FUZZ_GRAPHICS)
// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 2) {
        return 0;
    }
    const std::size_t width = data[0] % 64U;
    const std::size_t height = data[1] % 64U;
    const auto input = as_input(data + 2, size - 2);

    lvr::assets::Palette palette{};
    (void)lvr::assets::load_palette(palette, input, data[0]);
    (void)lvr::assets::decode_planar(input, width, height);
    (void)lvr::assets::decode_screen_image(input);
    const auto tileset = lvr::assets::decode_tileset(input.first(input.size() - (input.size() % 64U)));
    const auto metatiles = lvr::assets::decode_metatiles(input.first(input.size() - (input.size() % 8U)));
    const auto map =
        lvr::assets::decode_tilemap(input.first(std::min(input.size(), width * height * 2)), width, height);
    if (tileset && metatiles && map) {
        (void)lvr::assets::render_tilemap(*map, *metatiles, *tileset);
    }

    // Sprites: one run of the fuzzed size, plus a second 8x8 run so mixed runs are covered too.
    (void)lvr::assets::decode_unpacked_frame(input, width, height);
    (void)lvr::assets::decode_packed32(input);
    const std::array<lvr::assets::FrameRun, 2> runs{{
        {.width = static_cast<std::uint16_t>(width), .height = static_cast<std::uint16_t>(height), .count = 1},
        {.width = 8, .height = 8, .count = static_cast<std::uint16_t>(data[0] % 4U)},
    }};
    (void)lvr::assets::decode_sprite_frames(input, lvr::assets::SpriteFormat::kUnpacked, runs);
    (void)lvr::assets::decode_sprite_frames(input, lvr::assets::SpriteFormat::kRaw, runs);
    (void)lvr::assets::decode_tiled(input, runs[0]);
    return 0;
}
#endif

// Level header parser on arbitrary input.
#if defined(LVR_FUZZ_LEVEL)
// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    (void)lvr::assets::parse_level_header(as_input(data, size));
    (void)lvr::assets::parse_viking_starts(as_input(data, size));
    return 0;
}
#endif

// Script bytecode: every instruction decoded and formatted, the code traced from its first bytes, and the object
// database parser with its full disassembly.
#if defined(LVR_FUZZ_SCRIPT)
// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto input = as_input(data, std::min<std::size_t>(size, 0x10000));
    for (std::size_t pc = 0; pc < input.size(); ++pc) {
        const auto instruction = lvr::assets::decode_instruction(input, static_cast<std::uint16_t>(pc));
        if (instruction) {
            if (instruction->length == 0 || pc + instruction->length > input.size()) {
                __builtin_trap();  // an instruction always lies inside the input
            }
            (void)lvr::assets::format_instruction(*instruction);
        }
    }
    const auto split = std::min<std::size_t>(size, 32);
    (void)lvr::assets::parse_engine_tables(as_input(data, split), as_input(data + split, size - split),
                                           as_input(data, std::min<std::size_t>(size, 256)), as_input(data, size));
    const std::array<std::uint16_t, 2> entries{0, 3};
    (void)lvr::assets::trace_code(input, entries);
    const auto database = lvr::assets::parse_object_database(as_input(data, size));
    if (database) {
        (void)lvr::assets::disassemble(*database);
    }
    return 0;
}
#endif

// Dialogue text segment on arbitrary input.
#if defined(LVR_FUZZ_TEXT)
// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    (void)lvr::assets::parse_dialogue_texts(as_input(data, size));
    return 0;
}
#endif

// The SNES ROM's data on arbitrary input: the input as what follows the chunk table's address (the table, then the
// chunks), every chunk decoded, chunk 0 loaded as a level header with its backdrop; and the input as tiles, colours
// and a backdrop's header on their own.
#if defined(LVR_FUZZ_SNES)
// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto input = as_input(data, size);
    (void)lvr::assets::decode_snes_tiles(input);
    (void)lvr::assets::parse_backdrop_header(input);
    lvr::assets::Palette palette{};
    (void)lvr::assets::load_snes_palette(palette, input, size % 256);
    std::vector<std::byte> image(lvr::assets::kSnesChunkTable);
    image.insert(image.end(), input.begin(), input.end());
    const auto rom = lvr::assets::SnesRom::parse(std::move(image));
    if (!rom) {
        return 0;
    }
    for (std::size_t index = 0; index < rom->chunk_count(); ++index) {
        (void)rom->chunk(index);
    }
    (void)lvr::assets::load_backdrop(*rom, 0);
    return 0;
}
#endif
