# All third-party code, pinned to exact versions (docs/decisions.md, ADR-0009). Bump deliberately.
include(FetchContent)

# Third-party code is not ours to fix: compile every target a dependency defines with -w (no warnings).
# SYSTEM on FetchContent_Declare already hides warnings from their *headers* in our code; this covers
# their own .c/.cpp files. Walks the dependency's directory tree, so new targets are covered automatically.
function(lvr_silence_dependency_warnings dir)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            target_compile_options(${target} PRIVATE -w)
        endif()
    endforeach()
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(subdir IN LISTS subdirs)
        lvr_silence_dependency_warnings("${subdir}")
    endforeach()
endfunction()

if(LVR_BUILD_APPS)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_X11 OFF CACHE BOOL "" FORCE)  # Linux: Wayland only
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.20
        GIT_SHALLOW TRUE
        SYSTEM EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(SDL3)
    lvr_silence_dependency_warnings(${sdl3_SOURCE_DIR})

    # SDL3_net (zlib): the sockets for co-op online, on the SDL3 above. Patched to set TCP_NODELAY and,
    # on Linux, TCP_QUICKACK on its stream sockets (it sets neither and gives no access to the socket), and, for
    # Windows, to rename its read() and write() for sockets, which mingw-w64's io.h has for files.
    set(SDLNET_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLNET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF)  # (its default is shared, on a shared SDL3; this one's is static)
    set(sdl_net_patch ${CMAKE_CURRENT_LIST_DIR}/sdl_net.patch)
    FetchContent_Declare(SDL3_net
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_net.git
        GIT_TAG release-3.2.0
        GIT_SHALLOW TRUE
        # (applied once: a source that has it already is left as it is; one with an earlier version is reset first)
        PATCH_COMMAND sh -c "git apply --reverse --check ${sdl_net_patch} 2>/dev/null || (git checkout -q -- . && git apply ${sdl_net_patch})"
        SYSTEM EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(SDL3_net)
    unset(BUILD_SHARED_LIBS)
    lvr_silence_dependency_warnings(${sdl3_net_SOURCE_DIR})
endif()

# ymfm: the OPL3 (YMF262) for the Classic sound. No tags upstream: a pinned commit. It has no
# CMake target of its own that fits, so its sources are built into lvr_ymfm in src/CMakeLists.txt.
FetchContent_Declare(ymfm
    GIT_REPOSITORY https://github.com/aaronsgiles/ymfm.git
    GIT_TAG 81aec25ccbb98f4873a255f7551ac4dadac59b4a
    SYSTEM)
FetchContent_GetProperties(ymfm)
if(NOT ymfm_POPULATED)
    FetchContent_Populate(ymfm)
endif()

# TinySoundFont: the SoundFont synthesizer for the Modern sound. Header-only, no tags
# upstream: a pinned commit; its implementation is built into lvr_tsf in src/CMakeLists.txt.
FetchContent_Declare(tsf
    GIT_REPOSITORY https://github.com/schellingb/TinySoundFont.git
    GIT_TAG a3621a19c6504885cd9583e6e26c35bc954fc8ec
    SYSTEM)
FetchContent_GetProperties(tsf)
if(NOT tsf_POPULATED)
    FetchContent_Populate(tsf)
endif()

FetchContent_Declare(glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v9.1.0
    GIT_SHALLOW TRUE
    SYSTEM EXCLUDE_FROM_ALL)

FetchContent_MakeAvailable(glaze)
lvr_silence_dependency_warnings(${glaze_SOURCE_DIR})

if(LVR_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.16.1
        GIT_SHALLOW TRUE
        SYSTEM EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(Catch2)
    lvr_silence_dependency_warnings(${catch2_SOURCE_DIR})
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
