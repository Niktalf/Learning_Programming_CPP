#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "taskq/convert.hpp"
#include "taskq/engine.hpp"
#include "taskq/log.hpp"
#include "taskq/registry.hpp"
#include "taskq/state.hpp"
#include "taskq/task_store.hpp"
#include "taskq/worker.hpp"

namespace taskq {

inline constexpr std::size_t default_list_limit = 20;

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

    // Этап 8: движок индекса задач по номеру — параметр запуска
    // (taskq_cli --engine=hash).
    explicit Service(EngineKind engine, const std::optional<std::filesystem::path>& log = std::nullopt)
        : store_(engine) {
        if (log) {
            log_.emplace(*log);
        }
    }

    // С журналом переходов: каждый переход состояния дописывается в файл
    // строкой «номер<TAB>из<TAB>в». По журналу видно, что случилось с
    // конкретной задачей; на этапе 17 рядом с ним появится снапшот.
    //
    // Конструктор бросает, если файл не открылся. Деструктор Service тогда
    // не вызовется, но уже построенные поля разрушатся корректно.
    explicit Service(const std::filesystem::path& log) : log_(std::in_place, log) {}

    // Разбор и исполнение одной строки. Ошибки бросаются: ошибка разбора —
    // ParseError с подсказкой «usage: …», ошибка исполнения — обёрнутая в
    // звено контекста «while executing …» (std::throw_with_nested). Ловит их
    // граница handle.
    [[nodiscard]] Response execute(std::string_view line);

    // Этап 6: все команды сервиса — в одном реестре. Обработчики — методы
    // Service; добавить команду — одна строка регистрации в
    // src/commands.cpp и метод в разделе «Команды» ниже.
    [[nodiscard]] static const CommandRegistry<Service>& commands();

    [[nodiscard]] const TaskStore& store() const noexcept { return store_; }

    // Часы сервиса. DELAY откладывает задачу до «сейчас + задержка», и тесту
    // нужно управлять этим «сейчас», не засыпая. На этапе 11 те же часы
    // понадобятся срокам аренды.
    using Clock = std::function<Timestamp()>;
    void set_clock(Clock clock) { clock_ = std::move(clock); }

    // Этап 8: срок аренды. Задача, выданная POLL, должна быть подтверждена
    // (ACK, FAIL) или возвращена (RELEASE) за это время; иначе REAP вернёт её
    // в очередь. Поиск истёкших аренд — по индексу по сроку, без перебора.
    static constexpr std::chrono::seconds default_lease_timeout{300};
    void set_lease_timeout(std::chrono::seconds timeout) noexcept { lease_timeout_ = timeout; }

private:
    // Выданная задача помнит очередь, из которой пришла, — возврат аренды
    // ставит её обратно туда же, — и держателя аренды.
    //
    // Держатель — weak_ptr: задача не удерживает воркера в живых. Исчез
    // воркер — weak_ptr истёк, и задача брошена. Это второе, более быстрое
    // основание для возврата задачи: не ждать истечения срока аренды (он
    // есть с этапа 8, expires), а увидеть, что держатель пропал.
    //
    // Пустой optional — анонимная аренда, POLL без имени воркера.
    struct Lease {
        Task task;
        std::string queue;
        std::optional<std::weak_ptr<Worker>> holder;
        std::string holder_name;   // имя остаётся, даже когда воркера уже нет
        Timestamp expires{};       // этап 8: срок аренды, ключ в lease_deadlines_

        [[nodiscard]] bool abandoned() const noexcept { return holder && holder->expired(); }
    };

    // --- Команды. Каждая — метод с аргументами нужных типов; реестр
    // разбирает строку по сигнатуре. Константный метод — команда, которая
    // только читает: const виден в сигнатуре, и STATUS ничего не изменит
    // по ошибке. -----------------------------------------------------------
    Response create(QueueName queue);
    Response submit(QueueName queue, std::optional<Priority> priority, Tail body);
    Response submit_batch(Batch batch);
    Response poll(QueueName queue, std::optional<WorkerName> worker);
    Response ack(TaskId id);
    Response fail(TaskId id);
    Response release(TaskId id);
    Response status(TaskId id) const;
    Response type_of(TaskId id) const;
    Response bump(TaskId id, std::optional<std::int64_t> step);
    Response add_worker(WorkerName worker);
    Response gone(WorkerName worker);
    Response reap();
    Response list(QueueName queue, std::optional<MinPriority> min_priority, std::optional<Kind> kind,
                  std::optional<Limit> limit, std::optional<After> after) const;

    // Домашнее задание этапа 6.
    Response delay(TaskId id, std::chrono::seconds by);
    Response purge(QueueName queue);
    Response peek(QueueName queue) const;

    // Постановка одной задачи и пакета — один путь: SUBMIT — пакет из одной.
    [[nodiscard]] Response submit_all(const std::vector<Batch::Item>& items);

    // Возврат аренды в очередь: Leased -> Pending. Общий для RELEASE и REAP.
    void return_lease(TaskId id);

    // Конец аренды — запись уходит и из leases_, и из индекса по сроку. Не
    // бросает: удаление по ключу из двух деревьев.
    void end_lease(TaskId id) noexcept;

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
    // Этап 8: индекс аренд по сроку — красно-чёрное дерево пар «когда
    // истекает, номер». Истёкшие аренды — начало дерева до now, без перебора
    // всех аренд. На этапе 11 его будет опрашивать фоновый сборщик.
    std::set<std::pair<Timestamp, TaskId>> lease_deadlines_;
    std::chrono::seconds lease_timeout_ = default_lease_timeout;
    std::map<TaskId, State> states_;

    // Реестр воркеров владеет ими; аренды только наблюдают.
    std::map<std::string, std::shared_ptr<Worker>, std::less<>> workers_;

    std::optional<AppendOnlyLog> log_;   // пусто — журнал не ведётся
    Clock clock_ = [] { return std::chrono::system_clock::now(); };

    // Номера выдаются последовательно. На этапе 12 счётчик станет атомарным,
    // и там придётся отдельно разобраться с порядком памяти.
    TaskId next_id_ = 1;
};

}  // namespace taskq
