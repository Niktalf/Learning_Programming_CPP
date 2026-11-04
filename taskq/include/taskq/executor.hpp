#pragma once

#include <map>
#include <string>
#include <vector>

#include "taskq/command.hpp"
#include "taskq/state.hpp"
#include "taskq/task_store.hpp"

namespace taskq {

// Ответ клиенту. Неудача внутри ядра — исключение; ok == false появляется
// только на границе handle, где исключение превращается в ответ.
struct Response {
    bool ok = true;
    std::string text;
};

// Сервис очереди: хранилище ожидающих задач, выданные воркерам задачи и
// состояние каждой задачи по автомату из state.hpp.
//
// Состояние хранится в одном месте — states_, — и меняется только через
// transition_to: так все переходы проходят через allowed(). Где лежит сама
// задача, следует из состояния: Pending — в очереди, Leased — в leases_,
// Done и Failed — нигде, тело больше не нужно.
class Service {
public:
    // Исполнение команды. Ошибки бросаются, каждая обёрнута в звено контекста
    // «while executing …» (std::throw_with_nested).
    [[nodiscard]] Response execute(const Command& command);

    [[nodiscard]] const TaskStore& store() const noexcept { return store_; }

private:
    // Выданная задача помнит очередь, из которой пришла: возврат аренды
    // ставит её обратно туда же.
    struct Lease {
        Task task;
        std::string queue;
    };

    [[nodiscard]] Response run(const Command& command);
    [[nodiscard]] Response submit_all(const std::vector<SubmitCommand>& items);
    [[nodiscard]] Response poll(std::string_view queue);

    // Текущее состояние; задачи, которой не было, — UnknownTaskError.
    [[nodiscard]] State state_of(TaskId id) const;

    // Проверка перехода по автомату — до любых изменений. Бросает
    // InvalidTransitionError, если переход запрещён.
    void require_transition(TaskId id, State to) const;

    // Фиксация перехода, уже проверенного require_transition. Не бросает:
    // запись существует, присваивание перечисления бросить не может.
    void transition_to(TaskId id, State to) noexcept;

    TaskStore store_;
    std::map<TaskId, Lease> leases_;
    std::map<TaskId, State> states_;

    // Номера выдаются последовательно. На этапе 12 счётчик станет атомарным,
    // и там придётся отдельно разобраться с порядком памяти.
    TaskId next_id_ = 1;
};

}  // namespace taskq
