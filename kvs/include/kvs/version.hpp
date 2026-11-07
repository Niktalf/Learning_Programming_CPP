#pragma once

#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kvs {

// Этап 5: версия записи и оператор <=>.
//
// Шаг первый — сравнение по умолчанию. = default даёт лексикографическое
// сравнение по полям в порядке объявления и заодно выводит operator==.
// Порядок полей здесь и есть порядок сравнения: сначала поколение, затем
// отметка времени. Переставить поля — значит молча изменить смысл.
struct PlainVersion {
    std::uint64_t generation = 0;   // сколько раз ключ записывали
    std::uint64_t timestamp = 0;    // логические часы хранилища

    auto operator<=>(const PlainVersion&) const = default;
};

// Шаг второй — появилось поле, которое в сравнении участвовать не должно:
// автор записи. Две записи одного поколения в один момент — одна версия,
// кто бы её ни сделал. = default теперь не подходит: он сравнил бы и автора.
//
// Ловушка, ради которой этот шаг существует: operator== выводится только из
// = default. Из написанного руками <=> он не выводится, и без отдельного
// объявления a == b просто не скомпилируется. Тест Version.EqualityIgnoresAuthor
// без этой строки не собрался бы.
struct RecordVersion {
    std::uint64_t generation = 0;
    std::uint64_t timestamp = 0;
    std::string author;   // не участвует в сравнении

    // Тип результата — std::strong_ordering: оба поля целые. Будь среди них
    // double, вывелся бы std::partial_ordering из-за NaN.
    std::strong_ordering operator<=>(const RecordVersion& other) const noexcept {
        if (const auto order = generation <=> other.generation; order != 0) {
            return order;
        }
        return timestamp <=> other.timestamp;
    }

    bool operator==(const RecordVersion& other) const noexcept {
        return generation == other.generation && timestamp == other.timestamp;
    }

    // Сериализация — чтобы версия удовлетворяла концепции Serializable.
    [[nodiscard]] std::vector<std::byte> to_bytes() const;
    [[nodiscard]] static RecordVersion from_bytes(std::span<const std::byte> bytes);
};

}  // namespace kvs
