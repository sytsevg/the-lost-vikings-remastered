#include "assets/objects.hpp"

#include <algorithm>
#include <utility>

namespace lvr::assets {

// --- Object database ---

std::expected<ObjectDatabase, Error> parse_object_database(std::span<const std::byte> data) {
    constexpr std::size_t kRecordsSize = kObjectTypeCount * kObjectRecordSize;
    if (data.size() > 0x10000) {
        return std::unexpected{Error::kBadSize};
    }
    if (data.size() < kRecordsSize) {
        return std::unexpected{Error::kTruncated};
    }
    ObjectDatabase database;
    database.records.resize(kObjectTypeCount);
    std::size_t first_script = data.size();
    for (std::size_t i = 0; i < kObjectTypeCount; ++i) {
        ObjectRecord& record = database.records[i];
        for (std::size_t b = 0; b < kObjectRecordSize; ++b) {
            record.bytes.at(b) = std::to_integer<std::uint8_t>(data[(i * kObjectRecordSize) + b]);
        }
        if (record.script() < kRecordsSize || record.script() >= data.size()) {
            return std::unexpected{Error::kOffsetOutOfRange};
        }
        first_script = std::min<std::size_t>(first_script, record.script());
    }
    std::vector<std::uint8_t> list;
    for (std::size_t pos = kRecordsSize; pos < first_script; ++pos) {
        const auto value = std::to_integer<std::uint8_t>(data[pos]);
        if (value == 0xff) {
            database.byte_lists.push_back(std::move(list));
            list.clear();
        } else {
            list.push_back(value);
        }
    }
    if (!list.empty()) {
        return std::unexpected{Error::kTruncated};  // the last list runs into the first script
    }
    database.bytes.assign(data.begin(), data.end());
    return database;
}

}  // namespace lvr::assets
