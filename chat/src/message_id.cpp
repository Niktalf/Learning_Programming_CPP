#include "chat/message_id.hpp"

#include <string>

#include "chat/errors.hpp"

namespace chat {
namespace {

// Целые — восемь байт от младшего к старшему, строка — длина и байты.
// Порядок байтов задан явно, а не взят у машины.
void put_u64(std::vector<std::byte>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
    }
}

[[noreturn]] void truncated(std::size_t size) {
    throw ParseError("truncated message id", std::to_string(size) + " bytes");
}

}  // namespace

std::vector<std::byte> MessageId::to_bytes() const {
    std::vector<std::byte> out;
    put_u64(out, seq);
    put_u64(out, timestamp);
    put_u64(out, author.size());
    for (const char c : author) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

MessageId MessageId::from_bytes(std::span<const std::byte> bytes) {
    std::size_t position = 0;
    const auto u64 = [&]() {
        if (bytes.size() - position < 8) {
            truncated(bytes.size());
        }
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= std::to_integer<std::uint64_t>(bytes[position++]) << shift;
        }
        return value;
    };
    MessageId id;
    id.seq = u64();
    id.timestamp = u64();
    const std::uint64_t length = u64();
    if (bytes.size() - position != length) {
        truncated(bytes.size());
    }
    id.author.reserve(length);
    for (; position < bytes.size(); ++position) {
        id.author.push_back(static_cast<char>(bytes[position]));
    }
    return id;
}

}  // namespace chat
