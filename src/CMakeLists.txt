# Every target under src/ (docs/decisions.md, ADR-0010).
#   Pure C++ (no SDL):                         lvr::assets (its only file access: reading the original game files),
#                                              lvr::audio, lvr::input
#   SDL3-based (only with LVR_BUILD_APPS):     lvr::image, lv-export, lv-replay, vikings-remaster
#   The engine (src/lvr/, ADR-0017):       lvr::engine (pure C++; so far the level model)
#   Tests (LVR_BUILD_TESTS):                   lvr_tests, from <dir>/tests/
#   Fuzzers (LVR_BUILD_FUZZERS):               fuzz_*, from assets/fuzz/

# --- assets: the decoders of the game's files (docs/decisions.md, ADR-0014), used by everything: the formats (fuzzed,
# assets/fuzz/), the catalogue of what DATA.DAT holds, and levels, scenes, images, sprites, object databases and
# dialogue texts as the files hold them ---
add_library(lvr_assets STATIC
    assets/assets.cpp
    assets/audio.cpp
    assets/data_dat.cpp
    assets/graphics.cpp
    assets/level.cpp
    assets/objects.cpp
    assets/reader.cpp
    assets/script.cpp
    assets/snes.cpp
    assets/sprites.cpp
    assets/text.cpp)
add_library(lvr::assets ALIAS lvr_assets)
target_include_directories(lvr_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
lvr_target_defaults(lvr_assets)

# --- input: keys and gamepad to the original's input word, remappable (docs/original/input.md); shared ---
add_library(lvr_input STATIC
    input/input.cpp)
add_library(lvr::input ALIAS lvr_input)
target_include_directories(lvr_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
lvr_target_defaults(lvr_input)

# ymfm (third party, BSD-3): its sources as they are, warnings off.
add_library(lvr_ymfm STATIC  # the OPL family only (ymfm_opl.cpp also has the OPL4's PCM and the Y8950's ADPCM)
    ${ymfm_SOURCE_DIR}/src/ymfm_opl.cpp
    ${ymfm_SOURCE_DIR}/src/ymfm_adpcm.cpp
    ${ymfm_SOURCE_DIR}/src/ymfm_pcm.cpp)
target_include_directories(lvr_ymfm SYSTEM PUBLIC ${ymfm_SOURCE_DIR}/src)
target_compile_options(lvr_ymfm PRIVATE -w)
set_target_properties(lvr_ymfm PROPERTIES CXX_CLANG_TIDY "")

# TinySoundFont (third party, MIT): its implementation in a file of its own, warnings off.
# Built as C++, where its shifts of negative values are defined (C++20); UBSan reports them in C.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/tsf.cpp "#define TSF_IMPLEMENTATION\n#include \"tsf.h\"\n")
add_library(lvr_tsf STATIC ${CMAKE_CURRENT_BINARY_DIR}/tsf.cpp)
target_include_directories(lvr_tsf SYSTEM PUBLIC ${tsf_SOURCE_DIR})
# -fno-sanitize=function: it calls its stream callbacks (void* first argument) through pointers typed for its own
# structs, which UBSan's function check reports; harmless on every ABI, and not ours to change.
target_compile_options(lvr_tsf PRIVATE -w -fno-sanitize=function)
set_target_properties(lvr_tsf PROPERTIES CXX_CLANG_TIDY "")

add_library(lvr_audio STATIC
    audio/gm.cpp
    audio/opl3.cpp
    audio/player.cpp
    audio/sequencer.cpp)
add_library(lvr::audio ALIAS lvr_audio)
target_include_directories(lvr_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lvr_audio PUBLIC lvr::assets PRIVATE lvr_ymfm lvr_tsf)
lvr_target_defaults(lvr_audio)

# --- The engine (docs/architecture.md) ---
add_library(lvr_engine STATIC
    lvr/blocks.cpp
    lvr/bombers.cpp
    lvr/camera.cpp
    lvr/chargers.cpp
    lvr/crane.cpp
    lvr/drops.cpp
    lvr/shooters.cpp
    lvr/shots.cpp
    lvr/switched.cpp
    lvr/switches.cpp
    lvr/walkers.cpp
    lvr/conversations.cpp
    lvr/dialogue.cpp
    lvr/hits.cpp
    lvr/gameover.cpp
    lvr/inventory.cpp
    lvr/kinds.cpp
    lvr/items.cpp
    lvr/level.cpp
    lvr/looks.cpp
    lvr/machines.cpp
    lvr/platforms.cpp
    lvr/minimap.cpp
    lvr/movement.cpp
    lvr/places.cpp
    lvr/play.cpp
    lvr/render.cpp
    lvr/cues.cpp
    lvr/ship_scenes.cpp
    lvr/title.cpp
    lvr/village_scenes.cpp
    lvr/sprites.cpp
    lvr/terrain.cpp
    lvr/tomator.cpp
    lvr/viking_abilities.cpp
    lvr/viking_harm.cpp
    lvr/viking_things.cpp
    lvr/viking_worlds.cpp
    lvr/vikings.cpp
    lvr/weapons.cpp
    lvr/world.cpp)
add_library(lvr::engine ALIAS lvr_engine)
target_include_directories(lvr_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lvr_engine PUBLIC lvr::assets)
lvr_target_defaults(lvr_engine)

# --- The engine as a game to play: its pictures and sound per refresh (what a front end needs) ---
add_library(lvr_session STATIC
    lvr/session.cpp)
add_library(lvr::session ALIAS lvr_session)
target_include_directories(lvr_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lvr_session PUBLIC lvr::assets lvr::engine lvr::audio)
lvr_target_defaults(lvr_session)

# --- The pixel-match harness (ADR-0017): replays, what an engine shows per refresh, the comparison ---
add_library(lvr_replay STATIC
    replay/player.cpp
    replay/golden.cpp
    replay/replay.cpp)
add_library(lvr::replay ALIAS lvr_replay)
target_include_directories(lvr_replay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lvr_replay PUBLIC lvr::assets lvr::engine lvr::audio)
lvr_target_defaults(lvr_replay)

# --- The level dump's pictures (lv-export's; plain C++, so the tests reach them without SDL) ---
add_library(lvr_level_dump STATIC
    exporter/level_dump.cpp)
add_library(lvr::level_dump ALIAS lvr_level_dump)
target_include_directories(lvr_level_dump PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lvr_level_dump PUBLIC lvr::assets lvr::engine)
lvr_target_defaults(lvr_level_dump)

if(LVR_BUILD_APPS)
    # --- image: indexed PNG load/save through SDL3, shared by lv-export and the game ---
    add_library(lvr_image STATIC
        image/png.cpp)
    add_library(lvr::image ALIAS lvr_image)
    target_include_directories(lvr_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(lvr_image PUBLIC lvr::assets PRIVATE SDL3::SDL3)
    lvr_target_defaults(lvr_image)

    # --- lv-export: dev tool, writes the game data out as PNG + JSON for inspection (not shipped) ---
    add_executable(lv-export exporter/main.cpp)
    target_link_libraries(lv-export PRIVATE lvr::assets lvr::engine lvr::level_dump lvr::image glaze::glaze)
    lvr_target_defaults(lv-export)

    # --- The GPU renderer (ADR-0006), on SDL_GPU. Its shaders are compiled to SPIR-V here (glslc) and built in. ---
    find_program(LVR_GLSLC glslc REQUIRED)
    set(lvr_shader_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${lvr_shader_dir})
    foreach(shader gpu_quad.vert gpu_quad.frag gpu_show.vert gpu_show.frag gpu_over.frag gpu_mark.frag)
        add_custom_command(
            OUTPUT ${lvr_shader_dir}/${shader}.inc
            COMMAND ${LVR_GLSLC} -O -mfmt=c -o ${lvr_shader_dir}/${shader}.inc
                    ${CMAKE_CURRENT_SOURCE_DIR}/lvr/${shader}
            DEPENDS lvr/${shader}
            VERBATIM)
        list(APPEND lvr_shaders ${lvr_shader_dir}/${shader}.inc)
    endforeach()
    add_library(lvr_gpu STATIC
        lvr/gpu.cpp
        ${lvr_shaders})
    add_library(lvr::gpu ALIAS lvr_gpu)
    target_include_directories(lvr_gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${lvr_shader_dir})
    target_link_libraries(lvr_gpu PUBLIC lvr::engine PRIVATE SDL3::SDL3)
    lvr_target_defaults(lvr_gpu)

    add_executable(lv-replay replay/main.cpp)
    target_link_libraries(lv-replay PRIVATE lvr::replay lvr::image lvr::gpu)
    lvr_target_defaults(lv-replay)

    # --- vikings-remaster: the game on the engine ---
    add_executable(vikings-remaster lvr/main.cpp)
    target_link_libraries(vikings-remaster PRIVATE lvr::session lvr::input lvr::image lvr::gpu SDL3::SDL3)
    lvr_target_defaults(vikings-remaster)

endif()

# --- Tests: <dir>/tests/test_<name>.cpp tests <dir>/<name>.* (docs/decisions.md, ADR-0010) ---
if(LVR_BUILD_TESTS)
    add_executable(lvr_tests
        assets/tests/test_assets.cpp
        assets/tests/test_audio.cpp
        assets/tests/test_catalog.cpp
        assets/tests/test_data_dat.cpp
        assets/tests/test_graphics.cpp
        assets/tests/test_hash.cpp
        assets/tests/test_level.cpp
        assets/tests/test_objects.cpp
        assets/tests/test_snes.cpp
        assets/tests/test_reader.cpp
        assets/tests/test_script.cpp
        assets/tests/test_sprites.cpp
        assets/tests/test_text.cpp
        audio/tests/test_gm.cpp
        audio/tests/test_opl3.cpp
        audio/tests/test_sequencer.cpp
        exporter/tests/test_level_dump.cpp
        input/tests/test_input.cpp
        replay/tests/test_golden.cpp
        replay/tests/test_replay.cpp
        lvr/tests/test_camera.cpp
        lvr/tests/test_conversations.cpp
        lvr/tests/test_dialogue.cpp
        lvr/tests/test_hits.cpp
        lvr/tests/test_kinds.cpp
        lvr/tests/test_worlds.cpp
    lvr/tests/test_places.cpp
    lvr/tests/test_vikings.cpp
        lvr/tests/test_level.cpp
        lvr/tests/test_minimap.cpp
        lvr/tests/test_movement.cpp
        lvr/tests/test_play.cpp
        lvr/tests/test_render.cpp
        lvr/tests/test_session.cpp
        lvr/tests/test_sprites.cpp
        lvr/tests/test_terrain.cpp
        lvr/tests/test_world.cpp)
    target_link_libraries(lvr_tests PRIVATE lvr::assets lvr::audio lvr::input lvr::engine
                                            lvr::level_dump lvr::replay lvr::session
                                            Catch2::Catch2WithMain)
    # Tests tagged [real-data] use the user's own game files when present and are skipped otherwise.
    target_compile_definitions(lvr_tests PRIVATE LVR_ORIGINAL_DIR="${PROJECT_SOURCE_DIR}/gamedata/original"
                                               LVR_SOUNDFONT="${PROJECT_SOURCE_DIR}/gamedata/GeneralUser-GS.sf2"
                                               LVR_REPLAYS_DIR="${PROJECT_SOURCE_DIR}/replays")
    lvr_target_warnings(lvr_tests)
    # SDL3-backed code is only built with LVR_BUILD_APPS.
    if(LVR_BUILD_APPS)
        target_sources(lvr_tests PRIVATE image/tests/test_png.cpp lvr/tests/test_gpu.cpp)
        target_link_libraries(lvr_tests PRIVATE lvr::image lvr::gpu)
    endif()
    include(Catch)
    # Catch2's tags become CTest labels.
    catch_discover_tests(lvr_tests ADD_TAGS_AS_LABELS)
endif()

# --- Fuzzers: libFuzzer targets for the decoders in assets/ (docs/decisions.md, ADR-0002). All of them live in assets/fuzz/fuzzers.cpp; each
# binary compiles it with one LVR_FUZZ_<TARGET> define.
#   cmake --preset fuzz && cmake --build --preset fuzz
#   ./build/fuzz/bin/fuzz_lzss -max_total_time=60
if(LVR_BUILD_FUZZERS)
    function(lvr_add_fuzzer name define)
        add_executable(${name} assets/fuzz/fuzzers.cpp)
        target_compile_definitions(${name} PRIVATE ${define})
        # Debian's libclang_rt.fuzzer is built against libstdc++ while we use libc++ (ADR-0002). The two use
        # different inline namespaces, so linking libstdc++ for the fuzzer runtime alone is safe. Named by file:
        # clang rewrites a plain -lstdc++ to libc++ when -stdlib=libc++ is set.
        target_link_libraries(${name} PRIVATE lvr::assets -l:libstdc++.so.6)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer)
        lvr_target_warnings(${name})
    endfunction()

    lvr_add_fuzzer(fuzz_lzss LVR_FUZZ_LZSS)
    lvr_add_fuzzer(fuzz_snes LVR_FUZZ_SNES)
    lvr_add_fuzzer(fuzz_archive LVR_FUZZ_ARCHIVE)
    lvr_add_fuzzer(fuzz_graphics LVR_FUZZ_GRAPHICS)
    lvr_add_fuzzer(fuzz_level LVR_FUZZ_LEVEL)
    lvr_add_fuzzer(fuzz_script LVR_FUZZ_SCRIPT)
    lvr_add_fuzzer(fuzz_text LVR_FUZZ_TEXT)
endif()
