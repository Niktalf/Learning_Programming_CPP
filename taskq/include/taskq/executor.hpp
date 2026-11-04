#pragma once

#include <map>
#include <string>

#include "taskq/command.hpp"
#include "taskq/task_store.hpp"

namespace taskq {

// Ответ вместо кода возврата: на этапе 3 неудача станет исключением, и
// разница между «ошибка» и «результат» окончательно уедет в систему типов.
struct Response {
    bool ok = true;
    std::string text;
};

// Сервис очереди: хранилище ожидающих задач плюс учёт тех, что уже выданы
// воркерам. Задача, забранная POLL, ещё не выполнена — её надо подтвердить
// ACK или отклонить FAIL. На этапе 3 это превратится в конечный автомат
// состояний, а пока состояний три: ждёт в очереди, выдана, завершена.
class Service {
public:
    [[nodiscard]] Response execute(const Command& command);

    [[nodiscard]] const TaskStore& store() const noexcept { return store_; }

private:
    TaskStore store_;
    std::map<TaskId, Task> in_flight_;     // выданы воркерам, ждут ACK или FAIL
    std::map<TaskId, bool> finished_;      // true — подтверждена, false — провалена

    // Номера выдаются последовательно. На этапе 12 счётчик станет атомарным,
    // и там придётся отдельно разобраться с порядком памяти.
    TaskId next_id_ = 1;
};

}  // namespace taskq
