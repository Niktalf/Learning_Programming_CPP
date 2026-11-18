#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "taskq/task.hpp"

namespace taskq {

// Этап 10: доменные события сервиса задач и порт их публикации.
//
// Событие — факт жизненного цикла задачи словами предметной области:
// поставлена, выдана, выполнена, провалена. Каждое соответствует переходу
// автомата состояний (state.hpp); здесь нет ни топика, ни партиции, ни
// типов клиента брокера. Как событие ляжет в брокер — ключ сообщения и
// тело, — решает адаптер в infrastructure/. Если в этот заголовок
// просочится тип клиента брокера, слой протёк, и цель taskq_domain
// перестанет собираться изолированно (taskq_domain_isolated).
//
// Четыре события постановки — переходы «вперёд». Ещё два — переходы,
// которые есть в автомате и без которых поток событий врёт:
// TaskReturned — Leased → Pending (RELEASE, истёкшая или брошенная аренда,
// повтор после FAIL), иначе подписчик увидел бы вторую выдачу задачи без
// объяснения; TaskCancelled — Pending → Cancelled (PURGE), иначе задача
// осталась бы у подписчика вечно ожидающей. docs/events.md.

struct TaskSubmitted {
    TaskId id = 0;
    std::string queue;
    int priority = 0;
};

struct TaskLeased {
    TaskId id = 0;
    std::string worker;   // пусто — анонимная выдача, POLL без воркера
};

struct TaskCompleted {
    TaskId id = 0;
};

struct TaskFailed {
    TaskId id = 0;
    std::string reason;   // «attempt 3 of 3»
};

struct TaskReturned {
    TaskId id = 0;
    std::string reason;   // released, expired, abandoned, retry 2 of 3
};

struct TaskCancelled {
    TaskId id = 0;
};

using DomainEvent =
    std::variant<TaskSubmitted, TaskLeased, TaskCompleted, TaskFailed, TaskReturned, TaskCancelled>;

// Задача, к которой относится событие, — у всех шести она есть.
[[nodiscard]] inline TaskId task_of(const DomainEvent& event) noexcept {
    return std::visit([](const auto& e) { return e.id; }, event);
}

// Последнее ли это событие задачи: после него переходов нет (state.hpp).
[[nodiscard]] inline bool is_final(const DomainEvent& event) noexcept {
    return std::holds_alternative<TaskCompleted>(event) || std::holds_alternative<TaskFailed>(event) ||
           std::holds_alternative<TaskCancelled>(event);
}

// Порт публикации. Объявлен в domain — рядом с событиями; реализации — в
// infrastructure (брокер) и в тестах (FakePublisher).
//
// Контракт: publish не блокируется на сети — на этапе 11 он будет
// вызываться без мьютекса шарда, и задерживать ответ клиенту он не вправе.
// Бросить publish может; что тогда делать, решает вызывающий (Service:
// событие теряется, команда остаётся выполненной — docs/events.md).
class IEventPublisher {
public:
    virtual ~IEventPublisher() = default;
    virtual void publish(const DomainEvent& event) = 0;

protected:
    IEventPublisher() = default;
    IEventPublisher(const IEventPublisher&) = default;
    IEventPublisher& operator=(const IEventPublisher&) = default;
};

}  // namespace taskq
