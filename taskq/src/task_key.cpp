#include "taskq/task_key.hpp"

#include <string>

#include "taskq/errors.hpp"

namespace taskq {
namespace {

// Целые — восемь байт от младшего к старшему, строка — длина и байты.
void put_u64(std::vector<std::byte>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
    }
}

[[noreturn]] void malformed(std::size_t size) {
    throw ParseError("malformed task key", std::to_string(size) + " bytes");
}

}  // namespace

std::vector<std::byte> TaskKey::to_bytes() const {
    std::vector<std::byte> out;
    // Приоритет знаковый; хранится как беззнаковое представление того же
    // значения, и обратное приведение его восстанавливает.
    put_u64(out, static_cast<std::uint64_t>(static_cast<std::int64_t>(priority)));
    put_u64(out, seq);
    put_u64(out, requester.size());
    for (const char c : requester) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

TaskKey TaskKey::from_bytes(std::span<const std::byte> bytes) {
    std::size_t position = 0;
    const auto u64 = [&]() {
        if (bytes.size() - position < 8) {
            malformed(bytes.size());
        }
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= std::to_integer<std::uint64_t>(bytes[position++]) << shift;
        }
        return value;
    };
    TaskKey key;
    key.priority = static_cast<int>(static_cast<std::int64_t>(u64()));
    key.seq = u64();
    const std::uint64_t length = u64();
    if (bytes.size() - position != length) {
        malformed(bytes.size());
    }
    for (; position < bytes.size(); ++position) {
        key.requester.push_back(static_cast<char>(bytes[position]));
    }
    return key;
}

}  // namespace taskq
