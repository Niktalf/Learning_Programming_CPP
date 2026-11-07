#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace chat {

// Этап 5: идентификатор сообщения и оператор <=>.
//
// Шаг первый — сравнение по умолчанию. = default даёт лексикографическое
// сравнение по полям в порядке объявления и заодно выводит operator==.
// Порядок полей здесь и есть порядок сравнения.
struct PlainMessageId {
    std::uint64_t seq = 0;         // номер в общей последовательности чата
    std::uint64_t timestamp = 0;   // миллисекунды от эпохи

    auto operator<=>(const PlainMessageId&) const = default;
};

// Шаг второй — поле, которое в сравнении участвовать не должно: автор.
// Одно сообщение, разосланное в три комнаты, — один идентификатор, и автор
// его не меняет. = default теперь не подходит: он сравнил бы и автора.
//
// Ловушка, ради которой этот шаг существует: operator== выводится только из
// = default. Из написанного руками <=> он не выводится, и без отдельного
// объявления a == b не скомпилируется. Тест MessageId.EqualityIgnoresAuthor
// без него не собрался бы.
struct MessageId {
    std::uint64_t seq = 0;
    std::uint64_t timestamp = 0;
    std::string author;   // не участвует в сравнении

    // Оба поля целые — std::strong_ordering. Будь среди них double,
    // вывелся бы std::partial_ordering из-за NaN.
    std::strong_ordering operator<=>(const MessageId& other) const noexcept {
        if (const auto order = seq <=> other.seq; order != 0) {
            return order;
        }
        return timestamp <=> other.timestamp;
    }

    bool operator==(const MessageId& other) const noexcept {
        return seq == other.seq && timestamp == other.timestamp;
    }

    // Сериализация — чтобы идентификатор удовлетворял концепции Serializable.
    [[nodiscard]] std::vector<std::byte> to_bytes() const;
    [[nodiscard]] static MessageId from_bytes(std::span<const std::byte> bytes);
};

}  // namespace chat
