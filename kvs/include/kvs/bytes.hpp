#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "kvs/concepts.hpp"
#include "kvs/errors.hpp"

namespace kvs {

// Простейший двоичный формат: целые — восемь байт от младшего к старшему,
// строки — длина и байты. Порядок байтов задан явно, а не взят у машины:
// снапшот этапа 17 должен читаться и на другой архитектуре.

void put_u64(std::vector<std::byte>& out, std::uint64_t value);
void put_string(std::vector<std::byte>& out, std::string_view text);

// Чтение с курсором. Нехватка байтов — ParseError: данные пришли извне.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::uint64_t u64();
    [[nodiscard]] std::string string();
    [[nodiscard]] bool done() const noexcept { return position_ == bytes_.size(); }

private:
    void require(std::size_t count) const;

    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

// Обобщённые функции поверх концепции. Ошибка в типе аргумента даёт одну
// строку диагностики — «constraints not satisfied: Serializable», — а не
// простыню неудачных подстановок; сравнение — в docs/concepts.md.
template <Serializable T>
[[nodiscard]] std::vector<std::byte> encode(const T& value) {
    return value.to_bytes();
}

template <Serializable T>
[[nodiscard]] T decode(std::span<const std::byte> bytes) {
    return T::from_bytes(bytes);
}

}  // namespace kvs
