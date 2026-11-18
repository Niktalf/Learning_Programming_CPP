#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "taskq/clock.hpp"
#include "taskq/convert.hpp"
#include "taskq/events.hpp"
#include "taskq/journal.hpp"
#include "taskq/lifecycle.hpp"
#include "taskq/registry.hpp"
#include "taskq/retry_policy.hpp"
#include "taskq/state.hpp"
#include "taskq/task_store.hpp"
#include "taskq/worker.hpp"

namespace taskq {

inline constexpr std::size_t default_list_limit = 20;

// Сервис очереди: хранилище ожидающих задач, выданные воркерам задачи и
// состояние каждой задачи по автомату из state.hpp.
//
// Состояние хранится в одном месте — Lifecycle (этап 9), — и меняется только
// через него: так все переходы проходят через allowed(). Где лежит сама
// задача, следует из состояния: Pending — в очереди, Leased — в leases_,
// Done и Failed — нигде, тело больше не нужно.
class Service {
public:
    // Этап 9: все зависимости — снаружи. Хранилище приходит готовым (с
    // индексом по номеру), часы и журнал — портами IClock и ILogSink,
    // политика повторов — значением. Сервис не знает ни файлов, ни путей,
    // ни движков; собирает его точка сборки make_service (compose.hpp). До
    // этапа 9 конструкторы принимали путь и имя движка и сами открывали
    // файл (docs/audit.md, запись 4).
    //
    // Журнал переходов: каждый переход состояния — строка «номер<TAB>из<TAB>в».
    // По журналу видно, что случилось с конкретной задачей; на этапе 17
    // рядом с ним появится снапшот. Часы не во владении: они живут у точки
    // сборки дольше сервиса.
    //
    // Этап 10: и издатель доменных событий — порт IEventPublisher
    // (events.hpp). Переходы автомата порождают события там же, где пишется
    // журнал переходов; публикуются они после того, как команда выполнена.
    explicit Service(TaskStore store = TaskStore(), const IClock& clock = system_clocks(),
                     std::unique_ptr<ILogSink> log = nullptr, RetryPolicy retry = RetryPolicy(),
                     std::unique_ptr<IEventPublisher> events = nullptr)
        : store_(std::move(store)), log_(std::move(log)), events_(std::move(events)), clock_(&clock),
          retry_(retry) {}

    // Разбор и исполнение одной строки. Ошибки бросаются: ошибка разбора —
    // ParseError с подсказкой «usage: …», ошибка исполнения — обёрнутая в
    // звено контекста «while executing …» (std::throw_with_nested). Ловит их
    // граница handle.
    [[nodiscard]] Response execute(std::string_view line);

    // Этап 6: все команды сервиса — в одном реестре. Обработчики — методы
    // Service; добавить команду — одна строка регистрации в
    // application/src/commands.cpp и метод в разделе «Команды» ниже.
    [[nodiscard]] static const CommandRegistry<Service>& commands();

    [[nodiscard]] const TaskStore& store() const noexcept { return store_; }

    // Сколько событий не удалось опубликовать: издатель бросил или не
    // хватило памяти на само событие. Переход при этом выполнен — задача в
    // том состоянии, которое сказал ответ (docs/events.md).
    [[nodiscard]] std::size_t lost_events() const noexcept { return lost_events_; }

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
        Instant expires{};         // этап 8: срок аренды, ключ в lease_deadlines_; этап 9 — монотонный

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

    // Этап 9: когда задача поставлена — по системным часам (IClock::wall()).
    Response info(TaskId id) const;

    // Постановка одной задачи и пакета — один путь: SUBMIT — пакет из одной.
    [[nodiscard]] Response submit_all(const std::vector<Batch::Item>& items);

    // Возврат аренды в очередь: Leased -> Pending. Общий для RELEASE, REAP и
    // повтора после FAIL; reason — для события TaskReturned.
    void return_lease(TaskId id, std::string_view reason);

    // Конец аренды — запись уходит и из leases_, и из индекса по сроку. Не
    // бросает: удаление по ключу из двух деревьев.
    void end_lease(TaskId id) noexcept;

    // Запись перехода в журнал, если он ведётся.
    void record(TaskId id, std::string_view from, State to);

    // Этап 10: событие о переходе, который уже зафиксирован. Не бросает:
    // переход назад не откатить, и если на событие не хватило памяти, оно
    // считается потерянным — так же, как при отказе издателя. make строит
    // событие; вызывается, только если события публикуются.
    template <class Make>
    void raise(Make make) noexcept {
        if (!events_) {
            return;
        }
        try {
            outbox_.push_back(make());
        } catch (...) {
            ++lost_events_;
        }
    }

    // Опубликовать накопленное командой. Не бросает: событие, которое не
    // ушло, считается потерянным.
    void publish_events() noexcept;

    TaskStore store_;
    Lifecycle states_;   // этап 9: состояния и проверка переходов — в одной точке
    // Этап 9: сколько раз задачу проваливали — для политики повторов.
    // Запись живёт, пока задача не завершена окончательно.
    std::map<TaskId, std::uint32_t> failures_;
    std::map<TaskId, Lease> leases_;
    // Этап 8: индекс аренд по сроку — красно-чёрное дерево пар «когда
    // истекает, номер». Истёкшие аренды — начало дерева до now, без перебора
    // всех аренд. На этапе 11 его будет опрашивать фоновый сборщик.
    std::set<std::pair<Instant, TaskId>> lease_deadlines_;
    std::chrono::seconds lease_timeout_ = default_lease_timeout;

    // Реестр воркеров владеет ими; аренды только наблюдают.
    std::map<std::string, std::shared_ptr<Worker>, std::less<>> workers_;

    std::unique_ptr<ILogSink> log_;   // пусто — журнал не ведётся
    // Этап 10: издатель (пусто — события не публикуются) и события текущей
    // команды, ещё не отданные ему. На этапе 11 они будут собираться под
    // мьютексом шарда, а публиковаться после его освобождения.
    std::unique_ptr<IEventPublisher> events_;
    std::vector<DomainEvent> outbox_;
    std::size_t lost_events_ = 0;
    const IClock* clock_;             // этап 9: часы — не во владении
    RetryPolicy retry_;               // этап 9: что делать после FAIL

    // Номера выдаются последовательно. На этапе 12 счётчик станет атомарным,
    // и там придётся отдельно разобраться с порядком памяти.
    TaskId next_id_ = 1;
};

}  // namespace taskq
