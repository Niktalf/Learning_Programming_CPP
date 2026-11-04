#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "taskq/command.hpp"
#include "taskq/log.hpp"
#include "taskq/state.hpp"
#include "taskq/task_store.hpp"
#include "taskq/worker.hpp"

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
    Service() = default;

    // С журналом переходов: каждый переход состояния дописывается в файл
    // строкой «номер<TAB>из<TAB>в». По журналу видно, что случилось с
    // конкретной задачей; на этапе 17 рядом с ним появится снапшот.
    //
    // Конструктор бросает, если файл не открылся. Деструктор Service тогда
    // не вызовется, но уже построенные поля разрушатся корректно.
    explicit Service(const std::filesystem::path& log) : log_(std::in_place, log) {}

    // Исполнение команды. Ошибки бросаются, каждая обёрнута в звено контекста
    // «while executing …» (std::throw_with_nested).
    [[nodiscard]] Response execute(const Command& command);

    [[nodiscard]] const TaskStore& store() const noexcept { return store_; }

private:
    // Выданная задача помнит очередь, из которой пришла, — возврат аренды
    // ставит её обратно туда же, — и держателя аренды.
    //
    // Держатель — weak_ptr: задача не удерживает воркера в живых. Исчез
    // воркер — weak_ptr истёк, и задача брошена. Это второе, более быстрое
    // основание для возврата задачи: не ждать истечения срока аренды (он
    // появится на этапе 11), а увидеть, что держатель пропал.
    //
    // Пустой optional — анонимная аренда, POLL без имени воркера.
    struct Lease {
        Task task;
        std::string queue;
        std::optional<std::weak_ptr<Worker>> holder;
        std::string holder_name;   // имя остаётся, даже когда воркера уже нет

        [[nodiscard]] bool abandoned() const noexcept { return holder && holder->expired(); }
    };

    [[nodiscard]] Response run(const Command& command);
    [[nodiscard]] Response submit_all(const std::vector<SubmitCommand>& items);
    [[nodiscard]] Response poll(const PollCommand& command);
    [[nodiscard]] Response reap();

    // Возврат аренды в очередь: Leased -> Pending. Общий для RELEASE и REAP.
    void return_lease(TaskId id);

    // Запись перехода в журнал, если он ведётся.
    void record(TaskId id, std::string_view from, State to);

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

    // Реестр воркеров владеет ими; аренды только наблюдают.
    std::map<std::string, std::shared_ptr<Worker>, std::less<>> workers_;

    std::optional<AppendOnlyLog> log_;   // пусто — журнал не ведётся

    // Номера выдаются последовательно. На этапе 12 счётчик станет атомарным,
    // и там придётся отдельно разобраться с порядком памяти.
    TaskId next_id_ = 1;
};

}  // namespace taskq
