#include "kvs/bytes.hpp"

#include <algorithm>
#include <iterator>
#include <string>

#include "kvs/ivalue.hpp"
#include "kvs/version.hpp"

namespace kvs {

void put_u64(std::vector<std::byte>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
    }
}

void put_string(std::vector<std::byte>& out, std::string_view text) {
    put_u64(out, text.size());
    // Этап 8: алгоритм вместо цикла — каждый символ в байт.
    std::ranges::transform(text, std::back_inserter(out), [](char c) { return static_cast<std::byte>(c); });
}

void ByteReader::require(std::size_t count) const {
    if (bytes_.size() - position_ < count) {
        throw ParseError("truncated data", std::to_string(bytes_.size()) + " bytes");
    }
}

std::uint64_t ByteReader::u64() {
    require(8);
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= std::to_integer<std::uint64_t>(bytes_[position_++]) << shift;
    }
    return value;
}

std::string ByteReader::string() {
    const std::uint64_t size = u64();
    require(size);
    std::string text(size, '\0');
    const auto first = bytes_.begin() + static_cast<std::ptrdiff_t>(position_);
    std::ranges::transform(first, first + static_cast<std::ptrdiff_t>(size), text.begin(),
                           [](std::byte b) { return static_cast<char>(b); });
    position_ += size;
    return text;
}

std::vector<std::byte> RecordVersion::to_bytes() const {
    std::vector<std::byte> out;
    put_u64(out, generation);
    put_u64(out, timestamp);
    put_string(out, author);
    return out;
}

RecordVersion RecordVersion::from_bytes(std::span<const std::byte> bytes) {
    ByteReader in(bytes);
    RecordVersion version{in.u64(), in.u64(), in.string()};
    if (!in.done()) {
        throw ParseError("trailing bytes after version", std::to_string(bytes.size()) + " bytes");
    }
    return version;
}

std::vector<std::byte> StringValue::to_bytes() const {
    std::vector<std::byte> out;
    put_string(out, data_);
    return out;
}

StringValue StringValue::from_bytes(std::span<const std::byte> bytes) {
    ByteReader in(bytes);
    StringValue value(in.string());
    if (!in.done()) {
        throw ParseError("trailing bytes after string", std::to_string(bytes.size()) + " bytes");
    }
    return value;
}

std::vector<std::byte> IntValue::to_bytes() const {
    std::vector<std::byte> out;
    put_u64(out, static_cast<std::uint64_t>(data_));
    return out;
}

IntValue IntValue::from_bytes(std::span<const std::byte> bytes) {
    ByteReader in(bytes);
    IntValue value(static_cast<std::int64_t>(in.u64()));
    if (!in.done()) {
        throw ParseError("trailing bytes after int", std::to_string(bytes.size()) + " bytes");
    }
    return value;
}

}  // namespace kvs
