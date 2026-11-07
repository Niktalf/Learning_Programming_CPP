#include "taskq/executor.hpp"

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
#include <variant>

#include "taskq/errors.hpp"
#include "taskq/overloaded.hpp"
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

// Короткое описание команды для цепочки контекста: глагол и аргумент.
[[nodiscard]] std::string describe(const Command& command) {
    return std::visit(
        overloaded{
            [](const CreateCommand& c) { return "CREATE " + std::string(c.queue); },
            [](const SubmitCommand& c) { return "SUBMIT " + std::string(c.queue); },
            [](const BatchCommand& c) {
                return "SUBMITBATCH of " + std::to_string(c.items.size());
            },
            [](const PollCommand& c) { return "POLL " + std::string(c.queue); },
            [](const AckCommand& c) { return "ACK " + std::to_string(c.id); },
            [](const FailCommand& c) { return "FAIL " + std::to_string(c.id); },
            [](const ReleaseCommand& c) { return "RELEASE " + std::to_string(c.id); },
            [](const StatusCommand& c) { return "STATUS " + std::to_string(c.id); },
            [](const TypeCommand& c) { return "TYPE " + std::to_string(c.id); },
            [](const BumpCommand& c) { return "BUMP " + std::to_string(c.id); },
            [](const WorkerCommand& c) { return "WORKER " + std::string(c.name); },
            [](const GoneCommand& c) { return "GONE " + std::string(c.name); },
            [](const ReapCommand&) { return std::string("REAP"); },
            [](const ListCommand& c) { return "LIST " + std::string(c.queue); },
        },
        command);
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
Response Service::submit_all(const std::vector<SubmitCommand>& items) {
    std::vector<std::pair<std::string_view, Task>> tasks;
    tasks.reserve(items.size());
    std::map<TaskId, State> added;
    TaskId id = next_id_;
    for (const SubmitCommand& item : items) {
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
Response Service::poll(const PollCommand& command) {
    // Воркер, от имени которого берут задачу, должен быть в реестре.
    std::shared_ptr<Worker> worker;
    if (command.worker) {
        const auto it = workers_.find(*command.worker);
        if (it == workers_.end()) {
            throw UnknownWorkerError(std::string(*command.worker));
        }
        worker = it->second;
    }

    const auto id = store_.front_id(command.queue);   // нет очереди — UnknownQueueError
    if (!id) {
        return Response{true, "(empty)"};   // пустая очередь — не ошибка
    }
    require_transition(*id, State::Leased);

    // Подготовка: узел map и копии строк. Здесь может бросить, но очередь
    // ещё не тронута. weak_ptr строится из shared_ptr без выделения памяти.
    Lease prepared{Task{}, std::string(command.queue), std::nullopt, std::string()};
    if (worker) {
        prepared.holder = std::weak_ptr<Worker>(worker);
        prepared.holder_name = worker->name();
    }
    const auto slot = leases_.try_emplace(*id, std::move(prepared)).first;

    // Фиксация: ни одна операция ниже не бросает.
    slot->second.task = std::move(*store_.take(command.queue));
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

Response Service::run(const Command& command) {
    return std::visit(
        overloaded{
            [&](const CreateCommand& c) {
                const bool created = store_.create_queue(std::string(c.queue));
                return Response{true, created ? "OK" : "OK, already exists"};
            },
            [&](const SubmitCommand& c) { return submit_all({c}); },
            [&](const BatchCommand& c) { return submit_all(c.items); },
            [&](const PollCommand& c) { return poll(c); },
            [&](const AckCommand& c) {
                require_transition(c.id, State::Done);
                leases_.erase(c.id);
                transition_to(c.id, State::Done);
                record(c.id, name(State::Leased), State::Done);
                return Response{true, "OK"};
            },
            [&](const FailCommand& c) {
                require_transition(c.id, State::Failed);
                leases_.erase(c.id);
                transition_to(c.id, State::Failed);
                record(c.id, name(State::Leased), State::Failed);
                return Response{true, "OK"};
            },
            [&](const ReleaseCommand& c) {
                return_lease(c.id);
                return Response{true, "OK"};
            },
            [&](const StatusCommand& c) {
                const State state = state_of(c.id);
                if (state == State::Pending) {
                    // Задача в состоянии Pending обязана лежать в очереди.
                    return Response{true, "pending in " + store_.queue_of(c.id).value()};
                }
                if (state == State::Leased) {
                    const Lease& lease = leases_.at(c.id);
                    if (!lease.holder) {
                        return Response{true, std::string("leased")};
                    }
                    return Response{true, "leased by " + lease.holder_name +
                                              (lease.abandoned() ? " (abandoned)" : "")};
                }
                return Response{true, std::string(name(state))};
            },
            [&](const TypeCommand& c) {
                const State state = state_of(c.id);
                if (state == State::Pending) {
                    return Response{true, std::string(store_.find(c.id)->get().type_name())};
                }
                if (state == State::Leased) {
                    return Response{true, std::string(leases_.at(c.id).task.type_name())};
                }
                // Тело завершённой задачи не хранится.
                throw WrongStateError(c.id, state, "TYPE");
            },
            [&](const BumpCommand& c) {
                // BUMP меняет только ожидающую задачу: менять аргумент работы,
                // которую воркер уже выполняет, бессмысленно.
                const State state = state_of(c.id);
                if (state != State::Pending) {
                    throw WrongStateError(c.id, state, "BUMP");
                }
                Task& task = store_.find(c.id)->get();
                // Этап 4: вид тела определяет класс, проверка — dynamic_cast
                // внутри as: указатель или nullptr.
                NumericPayload* const number = task.as<NumericPayload>();
                if (number == nullptr) {
                    throw TypeMismatchError(c.id, "number", std::string(task.type_name()));
                }
                // Сначала вычисляем, потом присваиваем: бросок из checked_add
                // оставляет значение прежним.
                number->set_value(checked_add(c.id, number->value(), c.by));
                return Response{true, std::to_string(number->value())};
            },
            [&](const WorkerCommand& c) {
                // make_shared: воркер и контрольный блок — одна аллокация.
                // Обратная сторона: память блока освободится, только когда
                // умрут и все weak_ptr на него — то есть когда завершатся
                // аренды, которые этот воркер держал.
                const bool created =
                    workers_.try_emplace(std::string(c.name), std::make_shared<Worker>(std::string(c.name)))
                        .second;
                return Response{true, created ? "OK" : "OK, already registered"};
            },
            [&](const GoneCommand& c) {
                // Реестр перестаёт владеть воркером. Его аренды об этом не
                // узнают и не должны: их weak_ptr истекают сами.
                const auto it = workers_.find(c.name);
                if (it == workers_.end()) {
                    throw UnknownWorkerError(std::string(c.name));
                }
                workers_.erase(it);
                return Response{true, "OK"};
            },
            [&](const ReapCommand&) { return reap(); },
            [&](const ListCommand& c) {
                const auto tasks = store_.tasks_where(
                    c.queue, c.min_priority,
                    [&c](const Task& task) { return !c.kind || task.type_name() == *c.kind; },
                    c.limit);
                if (tasks.empty()) {
                    return Response{true, "(empty)"};
                }
                // transform превращает задачи в строки прямо в конвейере,
                // join_with склеивает их без ручного счёта разделителей.
                auto lines = tasks | std::views::transform([](const Task& task) {
                                 return std::format("id={} prio={} {}", task.id(), task.priority(),
                                                    task.to_string());
                             }) |
                             std::views::join_with('\n');
                return Response{true, std::ranges::to<std::string>(lines)};
            },
        },
        command);
}

// Звёздочка этапа 3: цепочка контекста. Ошибка из глубины — из хранилища или
// из автомата — не знает, какая команда её вызвала. Исполнитель перехватывает
// её только затем, чтобы приложить это знание, и тут же бросает дальше:
// std::throw_with_nested создаёт новое исключение, внутри которого лежит
// исходное. Ничего не решается и не проглатывается — разворачивает цепочку и
// решает, что ответить, по-прежнему только граница handle.
Response Service::execute(const Command& command) {
    try {
        return run(command);
    } catch (...) {
        std::throw_with_nested(TaskQError("while executing " + describe(command)));
    }
}

}  // namespace taskq
