#pragma once

// 64-bit FNV-1a: stable across platforms and builds. Identifies the original game files (releases.md) and gives
// reference values in tests. Not cryptographic. Header-only.

#include <cstddef>
#include <cstdint>
#include <span>

namespace lvr::assets {

class Fnv1a64 {
  public:
    void update(std::span<const std::byte> data) noexcept {
        for (const std::byte b : data) {
            hash_ ^= std::to_integer<std::uint64_t>(b);
            hash_ *= kPrime;
        }
    }
    std::uint64_t value() const noexcept { return hash_; }

  private:
    static constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325ULL;
    static constexpr std::uint64_t kPrime = 0x100000001b3ULL;
    std::uint64_t hash_ = kOffsetBasis;
};

inline std::uint64_t fnv1a64(std::span<const std::byte> data) noexcept {
    Fnv1a64 hasher;
    hasher.update(data);
    return hasher.value();
}

}  // namespace lvr::assets
