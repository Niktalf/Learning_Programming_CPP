#pragma once

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <generator>
#include <map>
#include <optional>
#include <stdexcept>
#include <ranges>
#include <string>
#include <string_view>
#include <limits>
#include <memory>
#include <variant>
#include <utility>
#include <vector>

#include "taskq/concepts.hpp"
#include "taskq/engine.hpp"
#include "taskq/errors.hpp"
#include "taskq/task.hpp"
#include "taskq/task_key.hpp"

namespace taskq {

// Хранилище параметризовано типом задачи по одной практической причине:
// в тестах вместо Task подставляется инструментированный тип, считающий
// копирования и перемещения. Без этого утверждение «постановка временной
// задачи не копирует» проверить нечем.
//
// Этап 5: требования к типу задачи записаны концепцией QueuedTask, а порядок
// выдачи определяет TaskKey: больший приоритет раньше, при равном — раньше
// поставленная.
//
// Этап 7: очередь — std::map по TaskKey, и задачи в ней лежат в порядке
// выдачи; вершина — begin(). Выбор — по замерам (docs/benchmarks.md) из трёх:
// priority_queue, отсортированный vector и дерево. Куча на выборке с вершины
// не хуже дерева, но у неё нет ни удаления произвольной задачи, ни обхода в
// порядке выдачи, ни способа пропустить отложенную задачу на вершине — а
// сервису нужны все три. Отсортированный vector быстрее всех на сотне задач
// и в сто раз медленнее дерева на 10^5: вставка сдвигает хвост.
//
// Ключ уникален — порядковый номер постановки не повторяется, — поэтому
// map, а не multimap: дерево то же, но find по ключу однозначен.
//
// Индекс по номеру задачи: STATUS, ACK, BUMP, DELAY ищут задачу по номеру.
// Этап 7 — std::unordered_map, в 75 раз быстрее дерева на 10^6 задач. Этап 8
// — за интерфейсом движка (engine.hpp): std::unordered_map или собственная
// хеш-таблица, выбор при запуске.
//
// Этап 8: отложенные задачи (DELAY) больше не лежат в очереди. Они в
// отдельном индексе по сроку — красно-чёрном дереве (std::map) по паре
// «когда готова, номер», — и очередь содержит только готовые. До этапа 8
// POLL проходил мимо отложенных на вершине, O(k); теперь вершина очереди —
// всегда готовая задача, а «кому пора» — начало индекса по сроку.
template <QueuedTask T>
class BasicTaskStore {
    // Типы — в начале класса: их используют вложенные итераторы.
    using Tasks = std::map<TaskKey, T>;

    // Отложенная задача: очередь, в которую она вернётся, и сама задача.
    struct Parked {
        std::string_view queue;
        T task;
    };

public:
    // Ключ индекса по сроку: момент и номер. Номер делает ключ уникальным —
    // у двух задач, отложенных до одного момента, разные номера.
    using Deadline = std::pair<Timestamp, TaskId>;

private:
    using ParkedTasks = std::map<Deadline, Parked>;

    // Где лежит задача: имя очереди и узел — в дереве очереди, если задача
    // готова, или в индексе по сроку, если отложена. Имя — представление
    // ключа queues_: ключи std::map не перемещаются, а очереди не
    // удаляются. Итератор узла std::map действителен, пока узел не удалён,
    // — вставки и удаления других задач его не трогают; на этом и стоит
    // индекс по номеру.
    struct Location {
        std::string_view queue;
        std::variant<typename Tasks::iterator, typename ParkedTasks::iterator> position;
    };

public:
    using Clock = std::chrono::system_clock;

    // Этап 8: индекс по номеру — за интерфейсом движка. Движок по умолчанию
    // — из окружения (default_engine_kind): так весь набор тестов
    // прогоняется на каждом движке без правок.
    BasicTaskStore() : BasicTaskStore(default_engine_kind()) {}
    explicit BasicTaskStore(EngineKind engine) : index_(make_task_index<Location>(engine)) {}

    [[nodiscard]] std::string_view engine_name() const noexcept { return index_->name(); }

    // --- Этап 7: собственный итератор по очереди -----------------------
    //
    // Прямой итератор по задачам очереди в порядке выдачи: первым — то, что
    // выдал бы POLL, если бы все задачи были готовы. Только чтение: обход
    // ничего не меняет — POLL забирает, обход смотрит. Обёртка над
    // итератором дерева, которая отдаёт задачу, а не пару с ключом: ключ —
    // подробность хранилища.
    //
    // Итератор помнит и конец своей очереди — для сравнения со стражем
    // QueueEnd (звёздочка). Сравнение двух итераторов — по позиции.
    class QueueIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = const T&;
        using pointer = const T*;

        QueueIterator() = default;

        [[nodiscard]] reference operator*() const noexcept { return position_->second; }
        [[nodiscard]] pointer operator->() const noexcept { return &position_->second; }

        QueueIterator& operator++() noexcept {
            ++position_;
            return *this;
        }
        QueueIterator operator++(int) noexcept {
            QueueIterator before = *this;
            ++position_;
            return before;
        }

        [[nodiscard]] friend bool operator==(const QueueIterator& a, const QueueIterator& b) noexcept {
            return a.position_ == b.position_;
        }

        // Звёздочка: сравнение со стражем. Итератор сам знает, где конец.
        struct End {};
        [[nodiscard]] friend bool operator==(const QueueIterator& it, End) noexcept {
            return it.position_ == it.end_;
        }

    private:
        friend class BasicTaskStore;
        QueueIterator(typename Tasks::const_iterator position, typename Tasks::const_iterator end) noexcept
            : position_(position), end_(end) {}

        typename Tasks::const_iterator position_{};
        typename Tasks::const_iterator end_{};
    };

    using QueueEnd = typename QueueIterator::End;

    // Очередь снаружи — представление: begin, end, size. Не владеет и не
    // даёт ничего изменить; живёт, пока живёт хранилище.
    class QueueView {
    public:
        [[nodiscard]] QueueIterator begin() const noexcept { return {tasks_->begin(), tasks_->end()}; }
        [[nodiscard]] QueueIterator end() const noexcept { return {tasks_->end(), tasks_->end()}; }
        [[nodiscard]] std::size_t size() const noexcept { return tasks_->size(); }
        [[nodiscard]] bool empty() const noexcept { return tasks_->empty(); }

        // Звёздочка этапа 7: те же задачи до стража. Диапазон не common —
        // конец другого типа. На этапе 13 так же будет выглядеть поток
        // байтов, конец которого заранее неизвестен.
        [[nodiscard]] std::ranges::subrange<QueueIterator, QueueEnd> until_end() const noexcept {
            return {begin(), QueueEnd{}};
        }

    private:
        friend class BasicTaskStore;
        explicit QueueView(const Tasks& tasks) noexcept : tasks_(&tasks) {}
        const Tasks* tasks_;
    };

    // Имя типа очереди для тех, кто её обходит (for_each_queue, view).
    using Queue = QueueView;

    bool create_queue(std::string queue) {
        // try_emplace из C++17: значение конструируется только при вставке.
        return queues_.try_emplace(std::move(queue)).second;
    }

    // Очередь по имени для обхода. Нет очереди — UnknownQueueError: обход
    // несуществующей очереди её не создаёт.
    template <NameLike N>
    [[nodiscard]] QueueView view(const N& queue) const {
        return QueueView(existing(name_view(queue)));
    }

    // Этап 3: очередь заводит только CREATE. Постановка в несуществующую
    // очередь — UnknownQueueError: опечатка в имени иначе уводила бы задачи
    // туда, куда никто не смотрит.
    //
    // Этап 7: две вставки — в дерево очереди и в индекс по номеру. Сильная
    // гарантия: если вторая бросит, первая откатывается удалением узла, а
    // удаление по итератору не бросает.
    void submit(std::string_view queue, T task) {
        const auto [name, tasks] = existing_entry(queue);
        task.set_sequence(seq_ + 1);
        const auto position = insert(tasks, std::move(task));
        try {
            add_to_index(name, position);
        } catch (...) {
            tasks.erase(position);
            throw;
        }
        ++seq_;   // после вставки: бросок номер не тратит
    }

    // Пакетная постановка с сильной гарантией: либо поставлены все задачи,
    // либо ни одной.
    //
    // Этап 7: до этапа 7 это был copy-and-swap — копии затронутых очередей
    // с дописанными задачами и обмен с оригиналами. С индексом по номеру он
    // не годится: индекс хранит итераторы узлов, а копия очереди — это
    // новые узлы, и после обмена итераторы смотрели бы в уничтоженную
    // копию. Теперь задачи вставляются на место, а при броске каждая
    // вставка откатывается. Откат состоит только из удалений по итератору
    // и по ключу-числу, а они не бросают, — поэтому откат сам не может
    // сорваться. Это второй способ получить сильную гарантию, наравне с
    // copy-and-swap: делать прямо и уметь отменить действиями без
    // исключений. Заодно он дешевле: копируются только новые задачи, а не
    // очереди целиком.
    //
    // Пакет принимается по значению и разбирается перемещением: вызывающий
    // отдаёт задачи, а не делится ими. Кому оригинал нужен (тесты гарантии),
    // тот передаёт копию — и все копирования случаются до первого изменения.
    void submit_batch(std::vector<std::pair<std::string_view, T>> items) {
        struct Inserted {
            Tasks* tasks;
            typename Tasks::iterator position;
            bool indexed;
        };
        std::vector<Inserted> done;
        done.reserve(items.size());                  // дальше push_back не выделяет
        index_->reserve(index_->size() + items.size());   // и индекс не рехешируется
        std::uint64_t seq = seq_;
        try {
            for (auto& [queue, task] : items) {
                const auto [name, tasks] = existing_entry(queue);
                task.set_sequence(++seq);
                done.push_back(Inserted{&tasks, insert(tasks, std::move(task)), false});
                add_to_index(name, done.back().position);
                done.back().indexed = true;
            }
        } catch (...) {
            for (const Inserted& entry : done) {
                if (entry.indexed) {
                    index_->erase(entry.position->second.id());
                }
                entry.tasks->erase(entry.position);
            }
            throw;
        }
        seq_ = seq;
    }

    // Этап 2: вместо сырого указателя — std::optional. Этап 3 развёл два
    // случая: отсутствующая очередь — UnknownQueueError, а пустой optional
    // означает ровно одно — очередь пуста. Возвращается копия задачи.
    //
    // Этап 6: «следующая» — среди готовых к выдаче. Этап 8: готовые — это
    // очередь плюс те отложенные задачи этой очереди, чей срок уже наступил,
    // но которые ещё не вернулись в очередь (promote_due).
    [[nodiscard]] std::optional<T> peek(std::string_view queue, Timestamp now = Clock::now()) const {
        const auto next = next_ready(queue, now);
        if (!next) {
            return std::nullopt;
        }
        return *next.task;
    }

    // Номер следующей задачи без копирования самой задачи. Нужен POLL, чтобы
    // подготовить место под задачу до того, как забрать её из очереди.
    [[nodiscard]] std::optional<TaskId> front_id(std::string_view queue, Timestamp now = Clock::now()) const {
        const auto next = next_ready(queue, now);
        if (!next) {
            return std::nullopt;
        }
        return next.task->id();
    }

    // Задача возвращается перемещением, без копии, и не бросает на
    // непустой очереди: перемещение T не бросает, удаление узла и записи
    // индекса — тоже. Поэтому take не возвращает отложенные задачи в
    // очередь сам (вставка в дерево выделяет память), а берёт готовую
    // отложенную прямо из индекса по сроку.
    [[nodiscard]] std::optional<T> take(std::string_view queue, Timestamp now = Clock::now()) {
        const auto next = next_ready(queue, now);
        if (!next) {
            return std::nullopt;
        }
        T taken = std::move(*next.task);
        index_->erase(taken.id());
        if (next.parked) {
            parked_.erase(next.held);
        } else {
            existing(queue).erase(next.ready);
        }
        return taken;
    }

    // --- Этап 6, домашнее задание: DELAY и PURGE -----------------------
    //
    // Отложить ожидающую задачу: до момента until её не выдадут. false —
    // задачи нет ни в одной очереди.
    //
    // Этап 8: задача переезжает из очереди в индекс по сроку. Сильная
    // гарантия: узел индекса выделяется и строится до любых изменений —
    // try_emplace сначала выделяет узел, затем перемещает в него задачу, и
    // если выделение бросило, задача ещё не тронута. Дальше удаление узла
    // очереди и правка записи индекса по номеру — не бросают. Повторный
    // DELAY переносит срок: узел извлекается и вставляется с новым ключом
    // (extract и insert узла памяти не выделяют).
    bool delay(TaskId id, Timestamp until) {
        Location* const location = index_->find(id);
        if (location == nullptr) {
            return false;
        }
        const Deadline deadline{until, id};
        if (auto* const held = std::get_if<typename ParkedTasks::iterator>(&location->position)) {
            auto node = parked_.extract(*held);
            node.key() = deadline;
            location->position = parked_.insert(std::move(node)).position;
            return true;
        }
        const auto ready = std::get<typename Tasks::iterator>(location->position);
        const auto parked = parked_.try_emplace(deadline, location->queue, std::move(ready->second)).first;
        existing(location->queue).erase(ready);
        location->position = parked;
        return true;
    }

    // Когда отложенная задача станет готова, или пусто, если не отложена.
    [[nodiscard]] std::optional<Timestamp> not_before(TaskId id) const {
        const Location* const location = index_->find(id);
        if (location == nullptr) {
            return std::nullopt;
        }
        const auto* const held = std::get_if<typename ParkedTasks::iterator>(&location->position);
        if (held == nullptr) {
            return std::nullopt;
        }
        return (*held)->first.first;
    }

    // --- Этап 8: индекс по сроку -----------------------------------------
    //
    // Отложенные задачи, которым пора: срок не позже now. Не перебор всех
    // задач, а начало дерева: upper_bound находит границу за O(log n), и
    // дальше — ровно k нужных. Этот запрос будет делать фоновый сборщик
    // этапа 11.
    [[nodiscard]] std::vector<TaskId> due(Timestamp now) const {
        return std::ranges::to<std::vector<TaskId>>(
            std::ranges::subrange(parked_.begin(), parked_.upper_bound(latest_at(now))) |
            std::views::transform([](const auto& entry) { return entry.first.second; }));
    }

    // Вернуть в очереди отложенные задачи, которым пора. Возвращает, сколько
    // вернулось. Каждая задача переносится с сильной гарантией — вставка в
    // дерево очереди до удаления из индекса по сроку, — а вся операция с
    // базовой: если на пятой задаче кончится память, первые четыре уже в
    // очередях, пятая — ещё в индексе по сроку, и для выдачи это одно и то
    // же: готовую отложенную задачу take и peek видят и там.
    //
    // Цикл, а не алгоритм: каждый шаг меняет само дерево, по которому идёт.
    std::size_t promote_due(Timestamp now) {
        std::size_t promoted = 0;
        while (!parked_.empty() && parked_.begin()->first.first <= now) {
            const auto held = parked_.begin();
            Tasks& tasks = existing(held->second.queue);
            TaskKey key = held->second.task.key();
            const auto ready = tasks.emplace(std::move(key), std::move(held->second.task)).first;
            index_->find(held->first.second)->position = ready;
            parked_.erase(held);
            ++promoted;
        }
        return promoted;
    }

    [[nodiscard]] std::size_t delayed_count() const noexcept { return parked_.size(); }

    // Номера всех ожидающих задач очереди — готовых и отложенных. Нужен
    // PURGE, чтобы проверить переходы до изменений.
    [[nodiscard]] std::vector<TaskId> pending_ids(std::string_view queue) const {
        std::vector<TaskId> ids;
        std::ranges::transform(existing(queue), std::back_inserter(ids),
                               [](const auto& entry) { return entry.second.id(); });
        std::ranges::transform(parked_in(queue), std::back_inserter(ids),
                               [](const auto& entry) { return entry.second.task.id(); });
        return ids;
    }

    // Убрать из очереди все ожидающие задачи — готовые и отложенные — и
    // вернуть их вызывающему. Сильная гарантия: вектор под задачи
    // выделяется до изменений; дальше обмен деревьев (не бросает),
    // перемещение задач (не бросает) и удаление узлов и записей индекса (не
    // бросает).
    [[nodiscard]] std::vector<T> purge(std::string_view queue) {
        Tasks& tasks = existing(queue);
        std::vector<T> removed;
        removed.reserve(tasks.size() + static_cast<std::size_t>(std::ranges::distance(parked_in(queue))));
        Tasks taken;
        taken.swap(tasks);
        for (auto& [key, task] : taken) {
            index_->erase(task.id());
            removed.push_back(std::move(task));
        }
        // Отложенные этой очереди. Цикл с it = erase(it): задачу надо забрать
        // до удаления узла, а erase_if только удаляет.
        for (auto it = parked_.begin(); it != parked_.end();) {
            if (it->second.queue != queue) {
                ++it;
                continue;
            }
            index_->erase(it->first.second);
            removed.push_back(std::move(it->second.task));
            it = parked_.erase(it);
        }
        return removed;
    }

    // --- Этап 5: LIST через ranges --------------------------------------
    //
    // Задачи очереди с приоритетом не ниже заданного, удовлетворяющие
    // предикату, — не больше limit. Конвейер:
    //
    //     filter(приоритет) | filter(предикат) | take
    //
    // Ничего не материализуется до take: тест List.PipelineIsLazy проверяет
    // это счётчиком.
    //
    // Этап 7: конвейер идёт по собственному итератору очереди, и задачи
    // приходят в порядке выдачи, а не постановки, как было у вектора: LIST
    // показывает очередь такой, какой её увидит POLL. Код конвейера этапа 5
    // не изменился — ranges нужен только forward_iterator.
    template <NameLike N, std::predicate<const T&> Predicate>
    [[nodiscard]] std::vector<std::reference_wrapper<const T>> tasks_where(const N& queue,
                                                                           int min_priority,
                                                                           Predicate predicate,
                                                                           std::size_t limit) const {
        const QueueView tasks = view(queue);
        // Предел обрезается по размеру очереди до take: take принимает
        // знаковую разность итераторов, и size_t больше PTRDIFF_MAX стал бы
        // отрицательным числом — нарушение предусловия counted_iterator.
        const auto bounded = static_cast<std::ptrdiff_t>(std::min(limit, tasks.size()));
        auto selected = tasks |
                        std::views::filter([min_priority](const T& task) {
                            return task.priority() >= min_priority;
                        }) |
                        std::views::filter([&predicate](const T& task) {
                            return std::invoke(predicate, task);
                        }) |
                        std::views::take(bounded);
        // Материализация — обычным циклом, а не std::ranges::to: ranges::to
        // для прямого диапазона проходит его дважды, сначала чтобы узнать
        // размер, и предикаты вызывались бы почти вдвое чаще.
        std::vector<std::reference_wrapper<const T>> result;
        for (const T& task : selected) {
            result.push_back(std::cref(task));
        }
        return result;
    }

    // --- Этап 8: LIST с курсором ------------------------------------------
    //
    // Страница — не больше limit готовых задач очереди после позиции after
    // (пусто — с начала) с теми же фильтрами, что у tasks_where, и признак,
    // что за страницей есть ещё. Курсор — ключ выдачи последней задачи
    // страницы, «приоритет и номер постановки»: upper_bound находит, откуда
    // продолжить, за O(log n), и ни одна задача, ждавшая в очереди всё время
    // обхода, не пропадёт и не повторится, что бы ни ставили и ни выдавали
    // между страницами. Задача, поставленная после начала обхода, может
    // оказаться перед курсором и в обход не попасть — как и у курсоров SCAN.
    //
    // На алгоритмах: subrange от upper_bound, два filter, take на одну
    // больше, чем нужно, — чтобы узнать, есть ли продолжение, — и transform
    // в результат.
    struct ListPage {
        std::vector<std::reference_wrapper<const T>> tasks;
        bool more = false;
    };

    template <NameLike N, std::predicate<const T&> Predicate>
    [[nodiscard]] ListPage list_page(const N& queue, int min_priority, Predicate predicate, std::size_t limit,
                                     const std::optional<TaskKey>& after) const {
        const Tasks& tasks = existing(name_view(queue));
        const auto first = after ? tasks.upper_bound(*after) : tasks.begin();
        const std::size_t bounded = std::min(limit, tasks.size());   // как в tasks_where
        auto selected = std::ranges::subrange(first, tasks.end()) | std::views::values |
                        std::views::filter([min_priority](const T& task) { return task.priority() >= min_priority; }) |
                        std::views::filter([&predicate](const T& task) { return std::invoke(predicate, task); }) |
                        std::views::take(static_cast<std::ptrdiff_t>(bounded + 1));
        ListPage page;
        std::ranges::transform(selected, std::back_inserter(page.tasks), [](const T& task) { return std::cref(task); });
        page.more = page.tasks.size() > bounded;
        if (page.more) {
            page.tasks.pop_back();
        }
        return page;
    }

    // Звёздочка этапа 5: задачи очереди генератором, с этапа 7 — в порядке
    // выдачи. Генератор смотрит в очередь: обходить его можно, только пока
    // она не меняется. Несуществующая очередь — пустая последовательность.
    [[nodiscard]] std::generator<const T&> tasks_lazily(std::string queue) const {
        const auto it = queues_.find(queue);
        if (it == queues_.end()) {
            co_return;
        }
        for (const auto& [key, task] : it->second) {
            co_yield task;
        }
    }

    template <NameLike N>
    [[nodiscard]] bool contains(const N& queue) const {
        return queues_.find(name_view(queue)) != queues_.end();
    }

    // Поиск ожидающей задачи по номеру для TYPE и BUMP. Этап 7: через индекс
    // — O(1) в среднем вместо перебора всех задач всех очередей.
    //
    // Этап 8: задача лежит либо в очереди, либо в индексе по сроку; visit
    // достаёт её из той позиции, которая записана.
    [[nodiscard]] std::optional<std::reference_wrapper<T>> find(TaskId id) {
        Location* const location = index_->find(id);
        if (location == nullptr) {
            return std::nullopt;
        }
        return std::ref(task_at(*location));
    }

    [[nodiscard]] std::optional<std::reference_wrapper<const T>> find(TaskId id) const {
        const Location* const location = index_->find(id);
        if (location == nullptr) {
            return std::nullopt;
        }
        return std::cref(task_at(*location));
    }

    // Имя очереди, в которой ждёт задача, — для STATUS.
    [[nodiscard]] std::optional<std::string> queue_of(TaskId id) const {
        const Location* const location = index_->find(id);
        if (location == nullptr) {
            return std::nullopt;
        }
        return std::string(location->queue);
    }

    [[nodiscard]] std::size_t queue_count() const noexcept { return queues_.size(); }

    // Сколько ожидающих задач в существующей очереди — готовых и
    // отложенных; отсутствующая — UnknownQueueError. Отложенные считаются
    // проходом по индексу по сроку — size нужен тестам и демонстрации, не
    // горячему пути.
    template <NameLike N>
    [[nodiscard]] std::size_t size(const N& queue) const {
        const std::string_view name = name_view(queue);
        return existing(name).size() + static_cast<std::size_t>(std::ranges::distance(parked_in(name)));
    }

    [[nodiscard]] bool empty() const noexcept { return queues_.empty(); }

    // Функтор принимается по значению — как это делают алгоритмы стандартной
    // библиотеки. Именно поэтому лямбда с захватом по значению и mutable не
    // выносит изменения наружу: меняется копия.
    //
    // Этап 7: очередь передаётся представлением — его можно обойти
    // range-based for, в порядке выдачи, но нельзя изменить.
    template <class F>
    void for_each_queue(F fn) const {
        std::ranges::for_each(queues_, [&fn](const auto& entry) { fn(entry.first, QueueView(entry.second)); });
    }

private:
    // Последний ключ индекса по сроку с моментом не позже now.
    [[nodiscard]] static Deadline latest_at(Timestamp now) noexcept {
        return Deadline{now, std::numeric_limits<TaskId>::max()};
    }

    // Отложенные задачи одной очереди — представление над индексом по
    // сроку. Проход по всему индексу: отложенных обычно мало, и нужен он
    // только PURGE и size.
    [[nodiscard]] auto parked_in(std::string_view queue) const {
        return parked_ | std::views::filter([queue](const auto& entry) { return entry.second.queue == queue; });
    }

    // Следующая задача к выдаче — лучшая по ключу выдачи среди двух
    // кандидатов: вершины очереди и лучшей из отложенных задач этой очереди,
    // чей срок наступил. Вторых обычно нет вовсе: POLL возвращает их в
    // очередь (promote_due) прежде, чем выбирать.
    //
    // Один код для константного и изменяющего хранилища — явный параметр
    // объекта из C++23 (deducing this): Self выводится как const или не
    // const BasicTaskStore, и итераторы в результате — соответствующие.
    // До C++23 пришлось бы писать две копии или const_cast.
    template <class Self>
    [[nodiscard]] auto next_ready(this Self& self, std::string_view queue, Timestamp now) {
        auto& tasks = self.existing(queue);
        using Ready = decltype(tasks.begin());
        using Held = decltype(self.parked_.begin());
        // Двойные скобки: decltype от выражения, а не от объявленного типа
        // поля, — так в нём остаётся const константного хранилища.
        using Item = std::remove_reference_t<decltype((tasks.begin()->second))>;
        struct Next {
            Item* task = nullptr;
            bool parked = false;
            Ready ready{};
            Held held{};
            explicit operator bool() const noexcept { return task != nullptr; }
        };

        auto due = std::ranges::subrange(self.parked_.begin(), self.parked_.upper_bound(latest_at(now))) |
                   std::views::filter([queue](const auto& entry) { return entry.second.queue == queue; });
        const auto best = std::ranges::min_element(
            due, {}, [](const auto& entry) { return entry.second.task.key(); });

        Next next;
        if (!tasks.empty()) {
            next = Next{&tasks.begin()->second, false, tasks.begin(), {}};
        }
        if (best != due.end() && (!next || best->second.task.key() < next.ready->first)) {
            next = Next{&best.base()->second.task, true, {}, best.base()};
        }
        return next;
    }

    [[nodiscard]] static T& task_at(Location& location) noexcept {
        return std::visit(
            [](auto position) -> T& {
                if constexpr (std::is_same_v<decltype(position), typename Tasks::iterator>) {
                    return position->second;
                } else {
                    return position->second.task;
                }
            },
            location.position);
    }

    [[nodiscard]] static const T& task_at(const Location& location) noexcept {
        return task_at(const_cast<Location&>(location));   // только чтение: итераторы хранятся неконстантными
    }

    [[nodiscard]] static typename Tasks::iterator insert(Tasks& tasks, T task) {
        TaskKey key = task.key();
        return tasks.emplace(std::move(key), std::move(task)).first;
    }

    void add_to_index(std::string_view queue, typename Tasks::iterator position) {
        if (!index_->insert(position->second.id(), Location{queue, position})) {
            throw std::logic_error("task id is already queued: " + std::to_string(position->second.id()));
        }
    }

    // Одна точка, где отсутствие очереди превращается в исключение.
    [[nodiscard]] Tasks& existing(std::string_view queue) { return existing_entry(queue).second; }

    [[nodiscard]] const Tasks& existing(std::string_view queue) const {
        const auto it = queues_.find(queue);
        if (it == queues_.end()) {
            throw UnknownQueueError(std::string(queue));
        }
        return it->second;
    }

    // Имя — из ключа таблицы: на него и смотрит индекс.
    [[nodiscard]] std::pair<std::string_view, Tasks&> existing_entry(std::string_view queue) {
        const auto it = queues_.find(queue);
        if (it == queues_.end()) {
            throw UnknownQueueError(std::string(queue));
        }
        return {it->first, it->second};
    }

    // std::less<> включает разнородный поиск: методы принимают string_view.
    std::map<std::string, Tasks, std::less<>> queues_;
    std::unique_ptr<ITaskIndex<Location>> index_;   // по номеру — за интерфейсом движка
    ParkedTasks parked_;                            // этап 8: индекс по сроку, отложенные задачи
    std::uint64_t seq_ = 0;   // последний выданный номер постановки
};

using TaskStore = BasicTaskStore<Task>;
using Queue = TaskStore::Queue;

}  // namespace taskq
