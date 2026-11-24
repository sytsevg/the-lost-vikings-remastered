// Tests for src/lvr/archive.hpp: values written with a fixed width, the low byte first, and read back the same.
#include <array>
#include <bitset>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lvr/archive.hpp"

using namespace lvr;

namespace {

enum class Doing : std::uint8_t { kWaits, kWalks, kFalls };
struct Inner {
    std::int16_t x = 0;
    bool there = false;
    bool operator==(const Inner&) const = default;
};
struct Sample {
    std::uint16_t timer = 0;
    bool turned = false;
    Doing doing = Doing::kWaits;
    std::array<std::int16_t, 2> last{};
    int slot = 0;
    std::size_t swing = 0;
    Inner inner;
    std::optional<std::uint8_t> maybe;
    std::array<Inner, 2> more{};
    bool operator==(const Sample&) const = default;
};
// A class that writes itself: its number twice.
class Own {
  public:
    int number = 0;
    void save(Writer& to) const {
        to(number);
        to(number);
    }
    void load(Reader& from) {
        from(number);
        from(number);
    }
};

template <class T>
std::vector<std::uint8_t> written(const T& value) {
    std::vector<std::uint8_t> bytes;
    Writer to{bytes};
    to(value);
    return bytes;
}

}  // namespace

TEST_CASE("archive: a plain struct is written field by field, numbers with a fixed width and the low byte first") {
    const Sample sample{.timer = 513,
                        .turned = true,
                        .doing = Doing::kFalls,
                        .last = {-2, 7},
                        .slot = -1,
                        .swing = 258,
                        .inner = {.x = 3, .there = true},
                        .maybe = 9,
                        .more = {{{.x = 1, .there = false}, {.x = -1, .there = true}}}};
    const std::vector<std::uint8_t> bytes = written(sample);
    CHECK(bytes == std::vector<std::uint8_t>{0x01, 0x02,                                      // timer
                                             0x01,                                            // turned
                                             0x02,                                            // doing
                                             0xfe, 0xff, 0x07, 0x00,                          // last
                                             0xff, 0xff, 0xff, 0xff,                          // slot: an int is 4
                                             0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // swing: a size is 8
                                             0x03, 0x00, 0x01,                                // inner
                                             0x01, 0x09,                                      // maybe: there, and what
                                             0x01, 0x00, 0x00, 0xff, 0xff, 0x01});            // more
    Sample back;
    Reader from{bytes};
    from(back);
    CHECK(from.done());
    CHECK(back == sample);

    // Nothing in the optional: only that.
    Sample empty;
    const std::vector<std::uint8_t> fewer = written(empty);
    CHECK(fewer.size() == bytes.size() - 1);
    Reader again{fewer};
    back.maybe = 4;
    again(back);
    CHECK(back == empty);
}

TEST_CASE("archive: vectors, strings and bitsets; a class that writes itself") {
    const std::vector<std::uint16_t> numbers{1, 2, 515};
    CHECK(written(numbers) == std::vector<std::uint8_t>{3, 0, 0, 0, 1, 0, 2, 0, 3, 2});
    const std::vector<bool> flags{true, false, true};
    CHECK(written(flags) == std::vector<std::uint8_t>{3, 0, 0, 0, 1, 0, 1});
    CHECK(written(std::string{"ab"}) == std::vector<std::uint8_t>{2, 0, 0, 0, 'a', 'b'});
    std::bitset<10> bits;
    bits.set(0);
    bits.set(9);
    CHECK(written(bits) == std::vector<std::uint8_t>{0x01, 0x02});
    Own own;
    own.number = 5;
    CHECK(written(own).size() == 8);

    std::vector<std::uint16_t> numbers_back{9};
    std::vector<bool> flags_back;
    std::string text_back;
    std::bitset<10> bits_back;
    Own own_back;
    std::vector<std::uint8_t> all;
    Writer to{all};
    to(numbers);
    to(flags);
    to(std::string{"ab"});
    to(bits);
    to(own);
    Reader from{all};
    from(numbers_back);
    from(flags_back);
    from(text_back);
    from(bits_back);
    from(own_back);
    CHECK(from.done());
    CHECK(numbers_back == numbers);
    CHECK(flags_back == flags);
    CHECK(text_back == "ab");
    CHECK(bits_back == bits);
    CHECK(own_back.number == 5);
}

TEST_CASE("archive: reading past the end, or a count that can't be, fails and gives nothing") {
    const std::vector<std::uint8_t> short_one{0x01};
    Reader from{short_one};
    std::uint16_t number = 7;
    from(number);
    CHECK_FALSE(from.ok());
    CHECK_FALSE(from.done());
    CHECK(number == 1);  // what was there, and nothing for the rest

    const std::vector<std::uint8_t> huge{0xff, 0xff, 0xff, 0x7f, 1, 2};
    Reader counted{huge};
    std::vector<std::uint8_t> values{5};
    counted(values);
    CHECK_FALSE(counted.ok());
    CHECK(values.empty());

    const std::vector<std::uint8_t> two{1, 2};
    Reader left_over{two};
    std::uint8_t one = 0;
    left_over(one);
    CHECK(left_over.ok());
    CHECK_FALSE(left_over.done());  // something is left
}

TEST_CASE("archive: something written with its length is read only by what takes exactly that much") {
    std::vector<std::uint8_t> bytes;
    Writer to{bytes};
    to.measured([](Writer& part) {
        part(std::uint16_t{7});
        part(true);
    });
    to(std::uint8_t{9});
    CHECK(bytes == std::vector<std::uint8_t>{3, 0, 0, 0, 7, 0, 1, 9});

    std::uint16_t number = 0;
    bool flag = false;
    std::uint8_t after = 0;
    Reader from{bytes};
    from.measured([&](Reader& part) {
        part(number);
        part(flag);
    });
    from(after);
    CHECK(from.done());
    CHECK(number == 7);
    CHECK(flag);
    CHECK(after == 9);

    // Read by something that takes less, or more: not what was written.
    Reader less{bytes};
    less.measured([&](Reader& part) { part(number); });
    CHECK_FALSE(less.ok());
    Reader more{bytes};
    std::uint32_t wide = 0;
    more.measured([&](Reader& part) { part(wide); });
    CHECK_FALSE(more.ok());
    // A length past the end.
    const std::vector<std::uint8_t> cut{9, 0, 0, 0, 1};
    Reader past{cut};
    past.measured([&](Reader& part) { part(number); });
    CHECK_FALSE(past.ok());
}
