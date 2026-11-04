#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "taskq/payload.hpp"

namespace taskq {

using TaskId = std::uint64_t;
using Timestamp = std::chrono::system_clock::time_point;

// Задача с семантикой значения поверх полиморфного тела.
//
// Тело лежит в std::unique_ptr<IPayload>: владелец один — задача. Снаружи
// задача по-прежнему копируемый тип, как на этапах 1–3, и поэтому хранилище
// и сильная гарантия пакетной постановки этапа 3 не изменились: копия
// очереди копирует задачи, а задача копирует тело через clone. Тесты этапа 3
// прошли без правок; Guarantee.BatchWithPolymorphicPayloads проверяет то же
// на настоящих задачах с бросающим clone.
//
// Правило нуля здесь перестаёт работать: поле unique_ptr не копируется, и
// копирующие операции приходится написать самим. Перемещение и деструктор
// по-прежнему генерирует компилятор.
class Task {
public:
    // Пустая задача без тела — только место, куда задачу потом переместят
    // (так поступает POLL). noexcept, без выделения памяти.
    Task() noexcept = default;

    Task(TaskId id, PayloadPtr payload, int priority = 0, Timestamp at = Timestamp{}) noexcept
        : id_(id), payload_(std::move(payload)), priority_(priority), at_(at) {}

    Task(TaskId id, std::string command, int priority = 0, Timestamp at = Timestamp{})
        : Task(id, std::make_unique<TextPayload>(std::move(command)), priority, at) {}

    Task(const Task& other)
        : id_(other.id_),
          payload_(other.payload_ ? other.payload_->clone() : nullptr),
          priority_(other.priority_),
          at_(other.at_) {}

    // copy-and-swap: копия снимается до того, как затрагивается *this, и
    // бросок clone оставляет задачу прежней.
    Task& operator=(const Task& other) {
        Task copy(other);
        swap(*this, copy);
        return *this;
    }

    Task(Task&&) noexcept = default;
    Task& operator=(Task&&) noexcept = default;
    ~Task() = default;

    friend void swap(Task& a, Task& b) noexcept {
        using std::swap;
        swap(a.id_, b.id_);
        a.payload_.swap(b.payload_);
        swap(a.priority_, b.priority_);
        swap(a.at_, b.at_);
    }

    [[nodiscard]] TaskId id() const noexcept { return id_; }
    [[nodiscard]] int priority() const noexcept { return priority_; }
    [[nodiscard]] Timestamp created_at() const noexcept { return at_; }

    // Задача без тела — пустая или перемещённая — отвечает «none», 0 и
    // пустой строкой: методы без предусловий обязаны работать и на ней.
    [[nodiscard]] std::string_view type_name() const noexcept {
        return payload_ ? payload_->type_name() : std::string_view("none");
    }
    [[nodiscard]] std::string to_string() const { return payload_ ? payload_->to_string() : std::string(); }
    [[nodiscard]] std::size_t size() const noexcept { return payload_ ? payload_->size() : 0; }

    // Доступ к конкретному виду тела: указатель или nullptr. Роль, которую на
    // этапе 2 играл std::get_if у variant; внутри теперь dynamic_cast.
    template <class T>
    [[nodiscard]] const T* as() const noexcept {
        return dynamic_cast<const T*>(payload_.get());
    }

    template <class T>
    [[nodiscard]] T* as() noexcept {
        return dynamic_cast<T*>(payload_.get());
    }

    friend bool operator==(const Task& a, const Task& b) noexcept {
        const bool same_payload = (!a.payload_ && !b.payload_) ||
                                  (a.payload_ && b.payload_ && a.payload_->equals(*b.payload_));
        return a.id_ == b.id_ && same_payload && a.priority_ == b.priority_ && a.at_ == b.at_;
    }
    friend bool operator!=(const Task& a, const Task& b) noexcept { return !(a == b); }

private:
    TaskId id_ = 0;
    PayloadPtr payload_;
    int priority_ = 0;
    Timestamp at_ = Timestamp{};
};

}  // namespace taskq
