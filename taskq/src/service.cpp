#include "taskq/service.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "taskq/errors.hpp"
#include "taskq/payload.hpp"

namespace taskq {
namespace {

constexpr std::string_view number_prefix = "num:";
constexpr std::string_view binary_prefix = "bin:";

[[nodiscard]] std::optional<std::int64_t> to_int(std::string_view text) noexcept {
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

[[nodiscard]] std::optional<std::string> from_hex(std::string_view text) {
    if (text.size() % 2 != 0) {
        return std::nullopt;
    }
    std::string bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = hex_digit(text[i]);
        const int low = hex_digit(text[i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<char>(high * 16 + low));
    }
    return bytes;
}

// Тело задачи по префиксу: num:42 — числовой аргумент, bin:cafe — двоичные
// данные в шестнадцатеричной записи, всё остальное — текстовая команда.
// Некорректный префиксный вид — ошибка разбора тела, а не тихий текст.
[[nodiscard]] PayloadPtr make_payload(std::string_view body) {
    if (body.substr(0, number_prefix.size()) == number_prefix) {
        const auto value = to_int(body.substr(number_prefix.size()));
        if (!value) {
            throw ParseError("num: payload must be a 64-bit integer", std::string(body));
        }
        return std::make_unique<NumericPayload>(*value);
    }
    if (body.substr(0, binary_prefix.size()) == binary_prefix) {
        auto bytes = from_hex(body.substr(binary_prefix.size()));
        if (!bytes) {
            throw ParseError("bin: payload must be an even number of hex digits",
                             std::string(body));
        }
        return std::make_unique<BinaryPayload>(std::move(*bytes));
    }
    return std::make_unique<TextPayload>(std::string(body));
}

// Сложение с проверкой переполнения. Необходимость нашёл фаззер из домашнего
// задания: переполнение знакового целого — неопределённое поведение.
[[nodiscard]] std::int64_t checked_add(TaskId id, std::int64_t current, std::int64_t by) {
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();
    if ((by > 0 && current > max - by) || (by < 0 && current < min - by)) {
        throw OverflowError(id, current, by);
    }
    return current + by;
}

// Строка задачи для LIST и PEEK.
[[nodiscard]] std::string line_of(const Task& task) {
    return std::format("id={} prio={} {}", task.id(), task.priority(), task.to_string());
}

}  // namespace

State Service::state_of(TaskId id) const {
    const auto it = states_.find(id);
    if (it == states_.end()) {
        throw UnknownTaskError(id);
    }
    return it->second;
}

void Service::require_transition(TaskId id, State to) const {
    const State from = state_of(id);
    if (!allowed(from, to)) {
        throw InvalidTransitionError(id, from, to);
    }
}

void Service::transition_to(TaskId id, State to) noexcept {
    states_.find(id)->second = to;
}

// Постановка одной задачи и пакета — один путь: SUBMIT — пакет из одной.
//
// Сильная гарантия на уровне сервиса складывается из трёх шагов, и порядок
// существен:
//   1. всё, что может бросить, но не трогает состояние сервиса: разбор тел
//      и подготовка записей о состоянии новых задач в отдельной таблице;
//   2. постановка в хранилище — сильная гарантия submit_batch;
//   3. перенос подготовленных записей в states_ через merge. merge переносит
//      узлы, не выделяя памяти, и бросить может только сравнение ключей —
//      а сравнение целых не бросает.
// Номера задач занимаются последними: неудачный пакет номеров не тратит.
Response Service::submit_all(const std::vector<Batch::Item>& items) {
    std::vector<std::pair<std::string_view, Task>> tasks;
    tasks.reserve(items.size());
    std::map<TaskId, State> added;
    TaskId id = next_id_;
    for (const Batch::Item& item : items) {
        tasks.emplace_back(item.queue, Task(id, make_payload(item.body), item.priority));
        added.emplace(id, State::Pending);
        ++id;
    }

    store_.submit_batch(tasks);
    states_.merge(added);
    for (TaskId submitted = next_id_; submitted < id; ++submitted) {
        record(submitted, "-", State::Pending);
    }

    const TaskId first = next_id_;
    next_id_ = id;
    if (items.size() == 1) {
        return Response{true, "OK id=" + std::to_string(first)};
    }
    return Response{true, "OK ids=" + std::to_string(first) + ".." + std::to_string(id - 1)};
}

// Выдача задачи воркеру — тоже с сильной гарантией. Наивный порядок «забрать
// из очереди, потом записать в leases_» терял бы задачу при нехватке памяти
// на узел map: из очереди она уже ушла, в аренду не попала. Поэтому место
// в leases_ готовится заранее, и задача переезжает в него перемещением,
// которое не бросает.
Response Service::poll(QueueName queue, std::optional<WorkerName> holder) {
    // Воркер, от имени которого берут задачу, должен быть в реестре.
    std::shared_ptr<Worker> worker;
    if (holder) {
        const auto it = workers_.find(holder->name);
        if (it == workers_.end()) {
            throw UnknownWorkerError(std::string(holder->name));
        }
        worker = it->second;
    }

    // Одно «сейчас» на всю команду: между front_id и take время не должно
    // сдвинуться так, чтобы выбор поменялся.
    const Timestamp now = clock_();
    const auto id = store_.front_id(queue.name, now);   // нет очереди — UnknownQueueError
    if (!id) {
        return Response{true, "(empty)"};   // пустая очередь — не ошибка
    }
    require_transition(*id, State::Leased);

    // Подготовка: узел map и копии строк. Здесь может бросить, но очередь
    // ещё не тронута. weak_ptr строится из shared_ptr без выделения памяти.
    Lease prepared{Task{}, std::string(queue.name), std::nullopt, std::string()};
    if (worker) {
        prepared.holder = std::weak_ptr<Worker>(worker);
        prepared.holder_name = worker->name();
    }
    const auto slot = leases_.try_emplace(*id, std::move(prepared)).first;

    // Фиксация: ни одна операция ниже не бросает.
    slot->second.task = std::move(*store_.take(queue.name, now));
    transition_to(*id, State::Leased);
    record(*id, name(State::Pending), State::Leased);

    return Response{true, std::format("id={} {}", *id, slot->second.task.to_string())};
}

void Service::return_lease(TaskId id) {
    // Возврат аренды: задача встаёт в конец своей очереди. Сначала копия в
    // очередь — если она бросит, аренда цела; затем удаление аренды, которое
    // не бросает. Копия — цена простоты: возврат редок по сравнению с POLL.
    require_transition(id, State::Pending);
    const auto lease = leases_.find(id);
    store_.submit(lease->second.queue, lease->second.task);
    leases_.erase(lease);
    transition_to(id, State::Pending);
    record(id, name(State::Leased), State::Pending);
}

// Возврат брошенных задач — тех, чей держатель исчез. На этапе 11 то же
// самое будет делать фоновый сборщик, вместе с проверкой срока аренды.
//
// Каждая задача возвращается с сильной гарантией, а вся операция — с
// базовой: если на пятой задаче кончится память, первые четыре уже в
// очередях, и это корректное состояние — каждая задача либо в очереди,
// либо в аренде, и ни одна не потеряна.
Response Service::reap() {
    std::vector<TaskId> abandoned;
    for (const auto& [id, lease] : leases_) {
        if (lease.abandoned()) {
            abandoned.push_back(id);
        }
    }
    for (const TaskId id : abandoned) {
        return_lease(id);
    }
    return Response{true, "OK returned=" + std::to_string(abandoned.size())};
}

void Service::record(TaskId id, std::string_view from, State to) {
    if (log_) {
        log_->append(std::to_string(id) + '\t' + std::string(from) + '\t' + std::string(name(to)));
    }
}

// --- Команды ---------------------------------------------------------

Response Service::create(QueueName queue) {
    const bool created = store_.create_queue(std::string(queue.name));
    return Response{true, created ? "OK" : "OK, already exists"};
}

// SUBMIT <queue> [prio=N] <body...>: необязательный именованный приоритет
// перед телом — то, что раньше требовало разбирать строку дважды. Теперь
// это просто сигнатура: optional<Priority>, затем Tail.
Response Service::submit(QueueName queue, std::optional<Priority> priority, Tail body) {
    return submit_all({Batch::Item{queue.name, priority ? priority->value : 0, body.text}});
}

Response Service::submit_batch(Batch batch) {
    return submit_all(batch.items);
}

Response Service::ack(TaskId id) {
    require_transition(id, State::Done);
    leases_.erase(id);
    transition_to(id, State::Done);
    record(id, name(State::Leased), State::Done);
    return Response{true, "OK"};
}

Response Service::fail(TaskId id) {
    require_transition(id, State::Failed);
    leases_.erase(id);
    transition_to(id, State::Failed);
    record(id, name(State::Leased), State::Failed);
    return Response{true, "OK"};
}

Response Service::release(TaskId id) {
    return_lease(id);
    return Response{true, "OK"};
}

Response Service::status(TaskId id) const {
    const State state = state_of(id);
    if (state == State::Pending) {
        // Задача в состоянии Pending обязана лежать в очереди.
        std::string text = "pending in " + store_.queue_of(id).value();
        if (const auto until = store_.not_before(id); until && *until > clock_()) {
            const auto left = std::chrono::ceil<std::chrono::seconds>(*until - clock_());
            text += std::format(", delayed for {}s", left.count());
        }
        return Response{true, std::move(text)};
    }
    if (state == State::Leased) {
        const Lease& lease = leases_.at(id);
        if (!lease.holder) {
            return Response{true, std::string("leased")};
        }
        return Response{true, "leased by " + lease.holder_name + (lease.abandoned() ? " (abandoned)" : "")};
    }
    return Response{true, std::string(name(state))};
}

Response Service::type_of(TaskId id) const {
    const State state = state_of(id);
    if (state == State::Pending) {
        return Response{true, std::string(store_.find(id)->get().type_name())};
    }
    if (state == State::Leased) {
        return Response{true, std::string(leases_.at(id).task.type_name())};
    }
    // Тело завершённой задачи не хранится.
    throw WrongStateError(id, state, "TYPE");
}

Response Service::bump(TaskId id, std::optional<std::int64_t> step) {
    // BUMP меняет только ожидающую задачу: менять аргумент работы, которую
    // воркер уже выполняет, бессмысленно.
    const State state = state_of(id);
    if (state != State::Pending) {
        throw WrongStateError(id, state, "BUMP");
    }
    Task& task = store_.find(id)->get();
    // Этап 4: вид тела определяет класс, проверка — dynamic_cast внутри as.
    NumericPayload* const number = task.as<NumericPayload>();
    if (number == nullptr) {
        throw TypeMismatchError(id, "number", std::string(task.type_name()));
    }
    // Сначала вычисляем, потом присваиваем: бросок из checked_add оставляет
    // значение прежним.
    number->set_value(checked_add(id, number->value(), step.value_or(1)));
    return Response{true, std::to_string(number->value())};
}

Response Service::add_worker(WorkerName worker) {
    // make_shared: воркер и контрольный блок — одна аллокация. Обратная
    // сторона: память блока освободится, только когда умрут и все weak_ptr
    // на него — то есть когда завершатся аренды, которые этот воркер держал.
    const std::string name(worker.name);
    const bool created = workers_.try_emplace(name, std::make_shared<Worker>(name)).second;
    return Response{true, created ? "OK" : "OK, already registered"};
}

Response Service::gone(WorkerName worker) {
    // Реестр перестаёт владеть воркером. Его аренды об этом не узнают и не
    // должны: их weak_ptr истекают сами.
    const auto it = workers_.find(worker.name);
    if (it == workers_.end()) {
        throw UnknownWorkerError(std::string(worker.name));
    }
    workers_.erase(it);
    return Response{true, "OK"};
}

// LIST <queue> [prio>=N] [type=K] [limit=N]: три именованных необязательных
// подряд — реестр принимает их в любом порядке.
Response Service::list(QueueName queue, std::optional<MinPriority> min_priority,
                       std::optional<Kind> kind, std::optional<Limit> limit) const {
    const auto tasks = store_.tasks_where(
        queue.name, min_priority ? min_priority->value : 0,
        [&kind](const Task& task) { return !kind || task.type_name() == kind->name; },
        limit ? limit->value : default_list_limit);
    if (tasks.empty()) {
        return Response{true, "(empty)"};
    }
    // transform превращает задачи в строки прямо в конвейере, join_with
    // склеивает их без ручного счёта разделителей.
    auto lines = tasks | std::views::transform([](const Task& task) { return line_of(task); }) |
                 std::views::join_with('\n');
    return Response{true, std::ranges::to<std::string>(lines)};
}

// --- Домашнее задание этапа 6 -------------------------------------------

// DELAY <id> <delay>: не выдавать задачу раньше «сейчас + задержка». Только
// ожидающую: отложить работу, которую уже выполняет воркер, нельзя. Задача
// остаётся в своей очереди на своём месте и с прежним приоритетом; POLL
// пропускает её, пока не наступит её время. Повторный DELAY переносит срок,
// DELAY 0 — снимает задержку.
Response Service::delay(TaskId id, std::chrono::seconds by) {
    const State state = state_of(id);
    if (state != State::Pending) {
        throw WrongStateError(id, state, "DELAY");
    }
    store_.delay(id, clock_() + by);
    return Response{true, std::format("OK, delayed for {}s", by.count())};
}

// PURGE <queue>: снять все ожидающие задачи очереди. Каждая переходит в
// Cancelled по автомату. Выданные воркерам задачи не трогаются: их судьбу
// решает воркер — ACK, FAIL или RELEASE.
//
// Сильная гарантия: проверки переходов — до изменений; затем очередь
// отдаётся целиком обменом (не бросает), и переходы фиксируются
// transition_to (не бросает). Запись в журнал — после фиксации, как и у
// остальных команд.
Response Service::purge(QueueName queue) {
    if (!store_.contains(queue.name)) {
        throw UnknownQueueError(std::string(queue.name));
    }
    store_.for_each_queue([&](const std::string& name, const Queue& tasks) {
        if (name == queue.name) {
            for (const Task& task : tasks) {
                require_transition(task.id(), State::Cancelled);
            }
        }
    });
    const Queue removed = store_.purge(queue.name);
    for (const Task& task : removed) {
        transition_to(task.id(), State::Cancelled);
    }
    for (const Task& task : removed) {
        record(task.id(), name(State::Pending), State::Cancelled);
    }
    return Response{true, std::format("OK purged={}", removed.size())};
}

// PEEK <queue>: что выдал бы POLL прямо сейчас, без выдачи. Константный
// метод: посмотреть — не значит изменить.
Response Service::peek(QueueName queue) const {
    const auto next = store_.peek(queue.name, clock_());
    if (!next) {
        return Response{true, "(empty)"};
    }
    return Response{true, line_of(*next)};
}

Response Service::execute(std::string_view line) {
    return commands().bind(line)(*this);
}

}  // namespace taskq
