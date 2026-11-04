#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace taskq {

using TaskId = std::uint64_t;
using Timestamp = std::chrono::system_clock::time_point;

// Правило нуля во плоти: ни одна из пяти специальных функций здесь не
// объявлена. Компилятор сгенерирует все пять сам, и они будут корректны,
// потому что все поля — число, строка и отметка времени — сами умеют
// корректно копироваться, перемещаться и разрушаться.
//
// Конструкторы правилу нуля не мешают. Его ломает объявление деструктора
// или любой из копирующих и перемещающих операций: объявленный деструктор
// подавляет генерацию перемещающих, и там, где ожидалось перемещение тела
// задачи, начнётся копирование.
class Task {
public:
    Task() = default;

    Task(TaskId id, std::string payload, int priority = 0, Timestamp at = Timestamp{})
        : id_(id), payload_(std::move(payload)), priority_(priority), at_(at) {}

    TaskId id() const noexcept { return id_; }
    const std::string& payload() const noexcept { return payload_; }
    int priority() const noexcept { return priority_; }
    Timestamp created_at() const noexcept { return at_; }
    std::size_t size() const noexcept { return payload_.size(); }

private:
    TaskId id_ = 0;
    std::string payload_;
    int priority_ = 0;
    Timestamp at_ = Timestamp{};
};

// Сравнение написано руками: генерация операторов по умолчанию появится
// только в C++20, вместе с оператором <=>.
inline bool operator==(const Task& lhs, const Task& rhs) noexcept {
    return lhs.id() == rhs.id() && lhs.payload() == rhs.payload() &&
           lhs.priority() == rhs.priority() && lhs.created_at() == rhs.created_at();
}

inline bool operator!=(const Task& lhs, const Task& rhs) noexcept {
    return !(lhs == rhs);
}

}  // namespace taskq
