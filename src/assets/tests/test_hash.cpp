// Tests for src/assets/hash.hpp
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <span>
#include <string_view>

#include "assets/hash.hpp"

namespace {

std::span<const std::byte> as_bytes(std::string_view text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}

}  // namespace

TEST_CASE("fnv1a64 matches the reference vectors") {
    CHECK(lvr::assets::fnv1a64(as_bytes("")) == 0xcbf29ce484222325ULL);
    CHECK(lvr::assets::fnv1a64(as_bytes("a")) == 0xaf63dc4c8601ec8cULL);
}

TEST_CASE("Fnv1a64 hashes incrementally exactly like one call") {
    lvr::assets::Fnv1a64 hasher;
    hasher.update(as_bytes("Lost "));
    hasher.update(as_bytes("Vikings"));
    CHECK(hasher.value() == lvr::assets::fnv1a64(as_bytes("Lost Vikings")));
}
