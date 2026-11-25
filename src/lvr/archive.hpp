#pragma once

// Writing the game's state out and reading it back: a quicksave in a file, and later a snapshot sent to another
// player. Numbers are written with a fixed width, the low byte first, whatever the machine; nothing is ever
// written as the memory it sits in. A plain struct (no base, nothing private: an aggregate) is written field by
// field without naming its fields, which is why every kind keeps what it remembers in one such struct (`Saved`,
// its member `s`: entity.hpp).
//
// What can be written: bool, the integers, enums, std::array, std::optional, std::vector, std::string,
// std::bitset, a plain struct of those, and a class with `save(Writer&) const` and `load(Reader&)` of its own.

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <glaze/reflection/to_tuple.hpp>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace lvr {

// The version of what is written of the game's state: at the start of a quicksave's file, and what two machines
// tell each other before one sends the other a snapshot. Whoever reads another version reads nothing. Raise it
// whenever a kind's `Saved`, `EntityState` or `WorldState` changes (a test in test_play.cpp says when the written
// size has changed without it).
inline constexpr std::uint32_t kStateVersion = 2;

class Writer;
class Reader;

namespace archive {

template <class T>
concept SavesItself = requires(const T& value, Writer& to) { value.save(to); };
template <class T>
concept LoadsItself = requires(T& value, Reader& from) { value.load(from); };

template <class T>
inline constexpr bool kIsArray = false;
template <class T, std::size_t N>
inline constexpr bool kIsArray<std::array<T, N>> = true;
template <class T>
inline constexpr bool kIsOptional = false;
template <class T>
inline constexpr bool kIsOptional<std::optional<T>> = true;
template <class T>
inline constexpr bool kIsVector = false;
template <class T>
inline constexpr bool kIsVector<std::vector<T>> = true;
template <class T>
inline constexpr bool kIsBitset = false;
template <std::size_t N>
inline constexpr bool kIsBitset<std::bitset<N>> = true;

// How many bytes an integer is written in: its own size, but always 8 for the types whose size differs between
// systems (long, and so std::size_t).
template <class T>
inline constexpr std::size_t kWidth =
    (std::is_same_v<T, long> || std::is_same_v<T, unsigned long> || sizeof(T) > 4) ? 8 : sizeof(T);

// Every field of a plain struct, in the order they are declared, given to `with`.
template <class T, class With, std::size_t... Index>
void each_field(T& value, With& with, std::index_sequence<Index...> /*unused*/) {
    auto fields = glz::to_tie(value);
    (with(glz::get<Index>(fields)), ...);
}
template <class T, class With>
void each_field(T& value, With& with) {
    // (No fields found means the struct can't be taken apart, not that it is empty: one of its members can't be
    // made from nothing, say. Nothing of it would be written, without a word.)
    static_assert(glz::detail::count_members<std::remove_cvref_t<T>> > 0, "a struct whose fields can't be found");
    each_field(value, with, std::make_index_sequence<glz::detail::count_members<std::remove_cvref_t<T>>>{});
}

}  // namespace archive

class Writer {
  public:
    explicit Writer(std::vector<std::uint8_t>& out) noexcept : out_{&out} {}

    template <class T>
    void operator()(const T& value) {
        if constexpr (archive::SavesItself<T>) {
            value.save(*this);
        } else if constexpr (std::is_same_v<T, bool>) {
            out_->push_back(value ? 1 : 0);
        } else if constexpr (std::is_enum_v<T>) {
            (*this)(static_cast<std::underlying_type_t<T>>(value));
        } else if constexpr (std::is_integral_v<T>) {
            const auto bits = static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<T>>(value));
            for (std::size_t byte = 0; byte < archive::kWidth<T>; ++byte) {
                out_->push_back(static_cast<std::uint8_t>(bits >> (8 * byte)));
            }
        } else if constexpr (archive::kIsArray<T>) {
            for (const auto& element : value) {
                (*this)(element);
            }
        } else if constexpr (archive::kIsOptional<T>) {
            (*this)(value.has_value());
            if (value) {
                (*this)(*value);
            }
        } else if constexpr (archive::kIsVector<T> || std::is_same_v<T, std::string>) {
            (*this)(static_cast<std::uint32_t>(value.size()));
            for (std::size_t i = 0; i < value.size();
                 ++i) {  // (by index: a vector of bool has no elements to refer to)
                (*this)(static_cast<typename T::value_type>(value[i]));
            }
        } else if constexpr (archive::kIsBitset<T>) {
            for (std::size_t bit = 0; bit < value.size(); bit += 8) {
                std::uint8_t byte = 0;
                for (std::size_t i = 0; i < 8 && bit + i < value.size(); ++i) {
                    byte = static_cast<std::uint8_t>(byte | (value[bit + i] ? 1U << i : 0U));
                }
                out_->push_back(byte);
            }
        } else {
            static_assert(std::is_aggregate_v<T>, "not something that can be written: see lvr/archive.hpp");
            archive::each_field(value, *this);
        }
    }

    // Something written with its length in bytes before it, so that whoever reads it can tell whether it is what
    // they would have written (a kind that remembers more or less by now is not).
    template <class Write>
    void measured(const Write& write) {
        std::vector<std::uint8_t> part;
        Writer to{part};
        write(to);
        (*this)(static_cast<std::uint32_t>(part.size()));
        out_->insert(out_->end(), part.begin(), part.end());
    }

  private:
    std::vector<std::uint8_t>* out_;
};

class Reader {
  public:
    explicit Reader(std::span<const std::uint8_t> in) noexcept : in_{in} {}

    // Everything asked for so far was there (what wasn't reads as nothing), and with `done` all of it was read.
    bool ok() const noexcept { return ok_; }
    bool done() const noexcept { return ok_ && at_ == in_.size(); }

    template <class T>
    void operator()(T& value) {
        if constexpr (archive::LoadsItself<T>) {
            value.load(*this);
        } else if constexpr (std::is_same_v<T, bool>) {
            value = byte() != 0;
        } else if constexpr (std::is_enum_v<T>) {
            std::underlying_type_t<T> number{};
            (*this)(number);
            value = static_cast<T>(number);
        } else if constexpr (std::is_integral_v<T>) {
            std::uint64_t bits = 0;
            for (std::size_t i = 0; i < archive::kWidth<T>; ++i) {
                bits |= std::uint64_t{byte()} << (8 * i);
            }
            value = static_cast<T>(static_cast<std::make_unsigned_t<T>>(bits));
        } else if constexpr (archive::kIsArray<T>) {
            for (auto& element : value) {
                (*this)(element);
            }
        } else if constexpr (archive::kIsOptional<T>) {
            bool there = false;
            (*this)(there);
            if (there) {
                (*this)(value.emplace());
            } else {
                value.reset();
            }
        } else if constexpr (archive::kIsVector<T> || std::is_same_v<T, std::string>) {
            std::uint32_t count = 0;
            (*this)(count);
            if (count > in_.size() - at_) {  // more than there can be: every element is a byte at least
                ok_ = false;
                count = 0;
            }
            value.clear();
            value.reserve(count);
            for (std::uint32_t i = 0; i < count && ok_; ++i) {
                typename T::value_type element{};
                (*this)(element);
                value.push_back(std::move(element));
            }
        } else if constexpr (archive::kIsBitset<T>) {
            for (std::size_t bit = 0; bit < value.size(); bit += 8) {
                const std::uint8_t bits = byte();
                for (std::size_t i = 0; i < 8 && bit + i < value.size(); ++i) {
                    value[bit + i] = (bits & (1U << i)) != 0;
                }
            }
        } else {
            static_assert(std::is_aggregate_v<T>, "not something that can be read: see lvr/archive.hpp");
            archive::each_field(value, *this);
        }
    }

    // What was written with measured(): read by `read`, which must take exactly as many bytes as were written.
    template <class Read>
    void measured(const Read& read) {
        std::uint32_t length = 0;
        (*this)(length);
        if (!ok_ || length > in_.size() - at_) {
            ok_ = false;
            return;
        }
        Reader part{in_.subspan(at_, length)};
        read(part);
        ok_ = part.done();
        at_ += length;
    }

  private:
    std::uint8_t byte() noexcept {
        if (at_ >= in_.size()) {
            ok_ = false;
            return 0;
        }
        return in_[at_++];
    }

    std::span<const std::uint8_t> in_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

// A class with a plain struct `Saved` after its base and nothing else of its own is exactly this large: what the
// kinds are held to (LVR_SAVES in entity.hpp).
template <class Base, class Saved>
struct WithSaved : Base {
    Saved s;
};

}  // namespace lvr
