#include "taskq/service.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <exception>
#include <format>
#include <iterator>
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

// Постановка одной задачи и пакета — один путь: SUBMIT — пакет из одной.
//
// Сильная гарантия на уровне сервиса складывается из трёх шагов, и порядок
// существен:
//   1. всё, что может бросить, но не трогает состояние сервиса: разбор тел
//      и подготовка записей о состоянии новых задач в отдельной таблице;
//   2. постановка в хранилище — сильная гарантия submit_batch;
//   3. перенос подготовленных записей в Lifecycle (admit — merge узлов:
//      памяти не выделяет, бросить может только сравнение целых, а оно не
//      бросает).
// Номера задач занимаются последними: неудачный пакет номеров не тратит.
Response Service::submit_all(const std::vector<Batch::Item>& items) {
    std::vector<std::pair<std::string_view, Task>> tasks;
    tasks.reserve(items.size());
    std::map<TaskId, State> added;
    TaskId id = next_id_;
    for (const Batch::Item& item : items) {
        // Этап 7: пара строится на месте — piecewise_construct передаёт
        // аргументы конструкторам обоих полей, и задача рождается прямо в
        // элементе вектора, без временного Task и его перемещения.
        tasks.emplace_back(std::piecewise_construct, std::forward_as_tuple(item.queue),
                           std::forward_as_tuple(id, make_payload(item.body), item.priority, clock_->wall()));
        added.emplace(id, State::Pending);
        ++id;
    }

    // Этап 7: пакет отдаётся хранилищу целиком, перемещением. До этапа 7
    // submit_batch принимал const& и копировал каждую задачу — то есть клон
    // тела на каждый SUBMIT ради вектора, который тут же уничтожался.
    store_.submit_batch(std::move(tasks));
    states_.admit(added);
    // Этап 8: iota вместо цикла со счётчиком — номера новых задач подряд.
    std::ranges::for_each(std::views::iota(next_id_, id), [this](TaskId submitted) {
        record(submitted, "-", State::Pending);
    });
    // Этап 10: по событию на задачу, в порядке номеров.
    for (std::size_t i = 0; i < items.size(); ++i) {
        raise([&] { return TaskSubmitted{next_id_ + i, std::string(items[i].queue), items[i].priority}; });
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
    const Instant now = clock_->now();
    // Этап 8: отложенные задачи, которым пора, — обратно в очереди. До
    // подготовки аренды: возврат может бросить (вставка в дерево), и тогда
    // аренды ещё нет, а каждая задача — в очереди или в индексе по сроку,
    // где take её тоже видит. Нет очереди — бросит front_id ниже.
    store_.promote_due(now);
    const auto id = store_.front_id(queue.name, now);   // нет очереди — UnknownQueueError
    if (!id) {
        return Response{true, "(empty)"};   // пустая очередь — не ошибка
    }
    states_.require(*id, State::Leased);

    // Подготовка: узел map и копии строк. Здесь может бросить, но очередь
    // ещё не тронута. weak_ptr строится из shared_ptr без выделения памяти.
    Lease prepared{Task{}, std::string(queue.name), std::nullopt, std::string()};
    if (worker) {
        prepared.holder = std::weak_ptr<Worker>(worker);
        prepared.holder_name = worker->name();
    }
    prepared.expires = now + lease_timeout_;   // этап 9: монотонные часы — срок не прыгает
    const auto slot = leases_.try_emplace(*id, std::move(prepared)).first;
    // Этап 8: узел индекса аренд по сроку — тоже в подготовке. Если его
    // выделение бросит, подготовленное место в leases_ убирается (erase по
    // итератору не бросает), и очередь не тронута.
    try {
        lease_deadlines_.emplace(slot->second.expires, *id);
    } catch (...) {
        leases_.erase(slot);
        throw;
    }

    // Фиксация: ни одна операция ниже не бросает.
    slot->second.task = std::move(*store_.take(queue.name, now));
    states_.commit(*id, State::Leased);
    record(*id, name(State::Pending), State::Leased);
    raise([&] { return TaskLeased{*id, slot->second.holder_name}; });

    return Response{true, std::format("id={} {}", *id, slot->second.task.to_string())};
}

void Service::return_lease(TaskId id, std::string_view reason) {
    // Возврат аренды: задача встаёт в конец своей очереди. Сначала копия в
    // очередь — если она бросит, аренда цела; затем удаление аренды, которое
    // не бросает. Копия — цена простоты: возврат редок по сравнению с POLL.
    states_.require(id, State::Pending);
    const auto lease = leases_.find(id);
    store_.submit(lease->second.queue, lease->second.task);
    end_lease(id);
    states_.commit(id, State::Pending);
    record(id, name(State::Leased), State::Pending);
    raise([&] { return TaskReturned{id, std::string(reason)}; });
}

void Service::end_lease(TaskId id) noexcept {
    const auto lease = leases_.find(id);
    if (lease == leases_.end()) {
        return;
    }
    lease_deadlines_.erase({lease->second.expires, id});
    leases_.erase(lease);
}

// Сборщик: возврат брошенных задач — тех, чей держатель исчез, — и, с
// этапа 8, задач с истёкшей арендой; заодно возврат в очереди отложенных
// задач, которым пора. На этапе 11 то же самое будет делать фоновый
// сборщик.
//
// Истёкшие аренды — не перебор: начало индекса по сроку до now, upper_bound
// за O(log n) и k нужных записей. Брошенные — перебор всех аренд: брошенность
// не время, её индекс по сроку не знает (этап 11 будет отмечать её в момент
// GONE).
//
// Задача может быть и брошенной, и просроченной — список собирается из двух
// и очищается от повторов идиомой erase-remove: sort, затем unique сдвигает
// уникальные в начало, erase убирает хвост.
//
// Каждая задача возвращается с сильной гарантией, а вся операция — с
// базовой: если на пятой задаче кончится память, первые четыре уже в
// очередях, и это корректное состояние — каждая задача либо в очереди,
// либо в аренде, и ни одна не потеряна.
Response Service::reap() {
    const Instant now = clock_->now();
    std::vector<TaskId> returned = std::ranges::to<std::vector<TaskId>>(
        std::ranges::subrange(lease_deadlines_.begin(),
                              lease_deadlines_.upper_bound({now, std::numeric_limits<TaskId>::max()})) |
        std::views::values);
    std::ranges::copy(leases_ | std::views::filter([](const auto& entry) { return entry.second.abandoned(); }) |
                          std::views::keys,
                      std::back_inserter(returned));
    std::ranges::sort(returned);
    returned.erase(std::ranges::unique(returned).begin(), returned.end());
    // Этап 10: причина — для события. Брошенная и просроченная одновременно
    // называется брошенной: держателя нет, и это важнее срока.
    std::ranges::for_each(returned, [this](TaskId id) {
        return_lease(id, leases_.at(id).abandoned() ? "abandoned" : "expired");
    });
    const std::size_t promoted = store_.promote_due(now);
    std::string text = "OK returned=" + std::to_string(returned.size());
    if (promoted != 0) {
        text += ", promoted=" + std::to_string(promoted);
    }
    return Response{true, std::move(text)};
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
    states_.require(id, State::Done);
    end_lease(id);
    states_.commit(id, State::Done);
    failures_.erase(id);
    record(id, name(State::Leased), State::Done);
    raise([&] { return TaskCompleted{id}; });
    return Response{true, "OK"};
}

// Этап 9: FAIL спрашивает политику повторов (RetryPolicy, домашнее
// задание). Попытки остались — задача возвращается в свою очередь и
// откладывается на паузу политики: Leased -> Pending, как у RELEASE. Не
// осталось — Leased -> Failed, как и до этапа 9. Политика по умолчанию —
// без повторов, и поведение по умолчанию прежнее.
Response Service::fail(TaskId id) {
    states_.require(id, State::Failed);   // FAIL — только для выданной задачи
    const auto known = failures_.find(id);
    const std::uint32_t failed = (known == failures_.end() ? 0 : known->second) + 1;
    if (const auto pause = retry_.retry_after(failed)) {
        // Место под счётчик — до изменений: try_emplace может бросить, но
        // ничего ещё не тронуто. Дальше возврат аренды с сильной гарантией;
        // если он бросит, только что созданный счётчик убирается.
        const auto [counter, created] = failures_.try_emplace(id, 0);
        try {
            return_lease(id, std::format("retry {} of {}", failed + 1, retry_.attempts()));
        } catch (...) {
            if (created) {
                failures_.erase(counter);
            }
            throw;
        }
        counter->second = failed;
        // Пауза: задача уже в очереди и ожидает. Если отложить её не выйдет
        // (узел индекса по сроку не выделился), она останется готовой без
        // паузы — состояние корректное, задача не потеряна.
        store_.delay(id, clock_->now() + *pause);
        return Response{true, std::format("OK, retry {} of {} in {}s", failed + 1, retry_.attempts(),
                                          pause->count())};
    }
    end_lease(id);
    states_.commit(id, State::Failed);
    failures_.erase(id);
    record(id, name(State::Leased), State::Failed);
    raise([&] { return TaskFailed{id, std::format("attempt {} of {}", failed, retry_.attempts())}; });
    return Response{true, "OK"};
}

Response Service::release(TaskId id) {
    return_lease(id, "released");
    return Response{true, "OK"};
}

Response Service::status(TaskId id) const {
    const State state = states_.state_of(id);
    if (state == State::Pending) {
        // Задача в состоянии Pending обязана лежать в очереди.
        std::string text = "pending in " + store_.queue_of(id).value();
        const Instant now = clock_->now();
        if (const auto until = store_.not_before(id); until && *until > now) {
            const auto left = std::chrono::ceil<std::chrono::seconds>(*until - now);
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
    const State state = states_.state_of(id);
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
    // Этап 9: правило «только ожидающая» — в одной точке, Lifecycle.
    states_.require_state(id, State::Pending, "BUMP");
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
    //
    // Этап 7: аргументы try_emplace вычисляются до вызова, и до этапа 7
    // make_shared выделял память и строил воркера даже для уже
    // зарегистрированного имени — чтобы тут же его выбросить. Теперь
    // lower_bound находит место, и воркер строится, только если его нет;
    // emplace_hint вставляет в найденное место без второго поиска.
    const auto place = workers_.lower_bound(worker.name);
    if (place != workers_.end() && place->first == worker.name) {
        return Response{true, "OK, already registered"};
    }
    const std::string name(worker.name);
    workers_.emplace_hint(place, name, std::make_shared<Worker>(name));
    return Response{true, "OK"};
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

// LIST <queue> [prio>=N] [type=K] [limit=N] [after=P.S]: четыре именованных
// необязательных подряд — реестр принимает их в любом порядке.
//
// Этап 8: с курсором. Если за страницей есть ещё задачи, последняя строка
// ответа — подсказка со следующим курсором: «(more: after=9.17)».
// Отложенные задачи LIST не показывает — их нет в очереди, они в индексе по
// сроку; о них говорит STATUS.
Response Service::list(QueueName queue, std::optional<MinPriority> min_priority, std::optional<Kind> kind,
                       std::optional<Limit> limit, std::optional<After> after) const {
    const auto page = store_.list_page(
        queue.name, min_priority ? min_priority->value : 0,
        [&kind](const Task& task) { return !kind || task.type_name() == kind->name; },
        limit ? limit->value : default_list_limit,
        after ? std::optional(TaskKey{after->priority, after->seq, std::string()}) : std::nullopt);
    if (page.tasks.empty()) {
        return Response{true, "(empty)"};
    }
    // transform превращает задачи в строки прямо в конвейере, join_with
    // склеивает их без ручного счёта разделителей.
    std::string text = std::ranges::to<std::string>(
        page.tasks | std::views::transform([](const Task& task) { return line_of(task); }) |
        std::views::join_with('\n'));
    if (page.more) {
        const Task& last = page.tasks.back();
        text += std::format("\n(more: after={}.{})", last.priority(), last.sequence());
    }
    return Response{true, std::move(text)};
}

// --- Домашнее задание этапа 6 -------------------------------------------

// DELAY <id> <delay>: не выдавать задачу раньше «сейчас + задержка». Только
// ожидающую: отложить работу, которую уже выполняет воркер, нельзя. Задача
// остаётся в своей очереди на своём месте и с прежним приоритетом; POLL
// пропускает её, пока не наступит её время. Повторный DELAY переносит срок,
// DELAY 0 — снимает задержку.
Response Service::delay(TaskId id, std::chrono::seconds by) {
    states_.require_state(id, State::Pending, "DELAY");
    store_.delay(id, clock_->now() + by);
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
    // Этап 8: ожидающие — это и готовые, и отложенные (pending_ids). Нет
    // очереди — UnknownQueueError. Проверки и фиксация — for_each.
    std::ranges::for_each(store_.pending_ids(queue.name),
                          [this](TaskId id) { states_.require(id, State::Cancelled); });
    const std::vector<Task> removed = store_.purge(queue.name);
    std::ranges::for_each(removed, [this](const Task& task) {
        states_.commit(task.id(), State::Cancelled);
        failures_.erase(task.id());
    });
    std::ranges::for_each(removed,
                          [this](const Task& task) { record(task.id(), name(State::Pending), State::Cancelled); });
    std::ranges::for_each(removed, [this](const Task& task) { raise([&] { return TaskCancelled{task.id()}; }); });
    return Response{true, std::format("OK purged={}", removed.size())};
}

// PEEK <queue>: что выдал бы POLL прямо сейчас, без выдачи. Константный
// метод: посмотреть — не значит изменить.
Response Service::peek(QueueName queue) const {
    const auto next = store_.peek(queue.name, clock_->now());
    if (!next) {
        return Response{true, "(empty)"};
    }
    return Response{true, line_of(*next)};
}

// INFO <id>: когда задача поставлена — по системным часам, в UTC. Время
// постановки показывают пользователю, поэтому оно настенное (IClock::wall),
// а не монотонное. Тело и время завершённой задачи не хранятся.
Response Service::info(TaskId id) const {
    const State state = states_.state_of(id);
    const Task* task = nullptr;
    if (state == State::Pending) {
        task = &store_.find(id)->get();
    } else if (state == State::Leased) {
        task = &leases_.at(id).task;
    } else {
        throw WrongStateError(id, state, "INFO");
    }
    const auto submitted = std::chrono::floor<std::chrono::seconds>(task->created_at());
    return Response{true, std::format("id={} {} submitted {:%FT%TZ} failures={}", id, name(state), submitted,
                                      failures_.contains(id) ? failures_.at(id) : 0u)};
}

Response Service::execute(std::string_view line) {
    try {
        Response response = commands().bind(line)(*this);
        publish_events();
        return response;
    } catch (...) {
        // Команды дают сильную гарантию, и неудачная событий не оставляет.
        // Кроме REAP: у него базовая, и задачи, возвращённые до отказа, уже
        // в очередях, — события о них уходят.
        publish_events();
        throw;
    }
}

void Service::publish_events() noexcept {
    // Сначала забрать: publish может бросить, и outbox_ должен опустеть в
    // любом случае — события этой команды не должны всплыть в следующей.
    std::vector<DomainEvent> taken;
    taken.swap(outbox_);
    for (const DomainEvent& event : taken) {
        try {
            events_->publish(event);
        } catch (...) {
            // Решение этапа 10 (docs/events.md, домашнее задание): событие
            // теряется, переход остаётся выполненным.
            ++lost_events_;
        }
    }
}

}  // namespace taskq
