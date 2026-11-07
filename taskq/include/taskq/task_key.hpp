#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace taskq {

// Этап 5: ключ выборки задачи и оператор <=>.
//
// Порядок выдачи задач: сначала больший приоритет, при равном — раньше
// поставленная. «Меньше» в смысле <=> значит «раньше выдать».
//
// Шаг первый — сравнение по умолчанию, и оно здесь неверно. = default даёт
// лексикографическое сравнение по полям в порядке объявления, по
// возрастанию каждого, а приоритет должен идти по убыванию. Тест
// TaskKey.DefaultOrderIsWrongForPriority фиксирует ошибку, которую = default
// делает молча: задача с приоритетом 0 «меньше» задачи с приоритетом 9 и
// выдавалась бы первой.
struct PlainTaskKey {
    int priority = 0;
    std::uint64_t seq = 0;

    auto operator<=>(const PlainTaskKey&) const = default;
};

// Шаг второй — порядок написан руками, и появилось поле, которое в сравнении
// не участвует: кто поставил задачу. Две задачи с одним приоритетом и
// номером — одно место в очереди, кто бы их ни поставил.
//
// Обратите внимание на первую строку <=>: операнды приоритета переставлены
// — other слева, this справа. Это и есть «по убыванию», и именно здесь чаще
// всего ошибаются; проявляется ошибка только на смешанных приоритетах,
// поэтому тест Service.PollRespectsPriorityThenOrder проверяет именно их.
//
// operator== из написанного руками <=> не выводится, его пришлось объявить.
struct TaskKey {
    int priority = 0;          // больше — раньше
    std::uint64_t seq = 0;     // меньше — раньше
    std::string requester;     // не участвует в сравнении

    std::strong_ordering operator<=>(const TaskKey& other) const noexcept {
        if (const auto order = other.priority <=> priority; order != 0) {
            return order;
        }
        return seq <=> other.seq;
    }

    bool operator==(const TaskKey& other) const noexcept {
        return priority == other.priority && seq == other.seq;
    }

    // Сериализация — чтобы ключ удовлетворял концепции Serializable.
    [[nodiscard]] std::vector<std::byte> to_bytes() const;
    [[nodiscard]] static TaskKey from_bytes(std::span<const std::byte> bytes);
};

}  // namespace taskq
