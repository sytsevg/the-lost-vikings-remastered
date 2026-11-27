cmake_minimum_required(VERSION 3.28)
project(vikings_remaster VERSION 1.0.0 LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "vikings-remaster builds with Clang only (docs/decisions.md, ADR-0002). Use a CMake preset: cmake --preset asan-ubsan")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All executables land in build/<preset>/bin/ instead of mirroring the source tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(LVR_BUILD_APPS "Build the SDL3-based programs: the game, lv-replay and lv-export" ON)
option(LVR_BUILD_TESTS "Build unit tests" ON)
option(LVR_BUILD_FUZZERS "Build libFuzzer targets" OFF)
option(LVR_WARNINGS_AS_ERRORS "Treat compiler and clang-tidy warnings as errors" OFF)
option(LVR_CLANG_TIDY "Run clang-tidy on project targets while compiling" OFF)
option(LVR_STATIC_RUNTIME "Link the C++ runtime statically into executables (docs/decisions.md, ADR-0013)" OFF)
option(LVR_COVERAGE "Instrument our own targets for clang source-based coverage (scripts/coverage.sh)" OFF)
set(LVR_SANITIZERS "" CACHE STRING "Sanitizers, semicolon-separated (e.g. address;undefined)")
set(LVR_LIBCXX_HARDENING "none" CACHE STRING "libc++ hardening mode")
set_property(CACHE LVR_LIBCXX_HARDENING PROPERTY STRINGS none fast extensive debug)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Toolchain)       # libc++, sanitizers, hardening: global, so dependencies get them too
include(Dependencies)    # FetchContent, pinned
include(TargetDefaults)  # warnings and clang-tidy: project targets only

if(LVR_BUILD_TESTS)
    enable_testing()
endif()

# Every target, including tests and fuzzers, is defined in src/CMakeLists.txt (docs/decisions.md, ADR-0010).
add_subdirectory(src)
