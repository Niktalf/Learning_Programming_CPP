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
#include <unordered_map>
#include <utility>
#include <vector>

#include "taskq/concepts.hpp"
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
// Индекс по номеру задачи — std::unordered_map: STATUS, ACK, BUMP, DELAY
// ищут задачу по номеру, и на 10^6 задач хеш-таблица в 75 раз быстрее дерева
// на поиске. До этапа 7 поиск по номеру был линейным перебором всех очередей.
template <QueuedTask T>
class BasicTaskStore {
    // Типы — в начале класса: их используют вложенные итераторы.
    using Tasks = std::map<TaskKey, T>;

    // Где лежит задача: имя очереди и узел в её дереве. Имя — представление
    // ключа queues_: ключи std::map не перемещаются, а очереди не
    // удаляются. Итератор узла std::map действителен, пока узел не удалён,
    // — вставки и удаления других задач его не трогают; на этом и стоит
    // индекс.
    //
    // not_before — время, раньше которого задачу не выдавать (DELAY).
    // Этап 6 держал его в отдельной таблице; с индексом по номеру ему место
    // здесь.
    struct Location {
        std::string_view queue;
        typename Tasks::iterator position;
        Timestamp not_before{};
    };

public:
    using Clock = std::chrono::system_clock;

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
        index_.reserve(index_.size() + items.size());   // и индекс не рехешируется
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
                    index_.erase(entry.position->second.id());
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
    // Этап 6: «следующая» — среди готовых к выдаче: задача, отложенная DELAY
    // до момента позже now, пропускается.
    [[nodiscard]] std::optional<T> peek(std::string_view queue, Timestamp now = Clock::now()) const {
        const Tasks& tasks = existing(queue);
        const auto next = next_in(tasks, now);
        if (next == tasks.end()) {
            return std::nullopt;
        }
        return next->second;
    }

    // Номер следующей задачи без копирования самой задачи. Нужен POLL, чтобы
    // подготовить место под задачу до того, как забрать её из очереди.
    [[nodiscard]] std::optional<TaskId> front_id(std::string_view queue, Timestamp now = Clock::now()) const {
        const Tasks& tasks = existing(queue);
        const auto next = next_in(tasks, now);
        if (next == tasks.end()) {
            return std::nullopt;
        }
        return next->second.id();
    }

    // Задача возвращается перемещением, без копии, и не бросает на
    // непустой очереди: перемещение T не бросает, удаление узла и записи
    // индекса — тоже.
    [[nodiscard]] std::optional<T> take(std::string_view queue, Timestamp now = Clock::now()) {
        Tasks& tasks = existing(queue);
        const auto next = next_in(tasks, now);
        if (next == tasks.end()) {
            return std::nullopt;
        }
        T taken = std::move(next->second);
        index_.erase(taken.id());
        tasks.erase(next);
        return taken;
    }

    // --- Этап 6, домашнее задание: DELAY и PURGE -----------------------
    //
    // Отложить ожидающую задачу: до момента until её не выдадут. false —
    // задачи нет ни в одной очереди. Отложенная задача остаётся в своей
    // очереди на своём месте: её видит LIST, но не POLL и не PEEK.
    //
    // Этап 7: дерево позволяет то, чего не позволила бы куча, — пропустить
    // отложенную задачу на вершине и взять следующую готовую. Цена —
    // проход мимо отложенных: O(k), где k — сколько их стоит впереди. На
    // этапе 8 их вынесет из очереди второй индекс, по времени готовности.
    bool delay(TaskId id, Timestamp until) {
        const auto it = index_.find(id);
        if (it == index_.end()) {
            return false;
        }
        it->second.not_before = until;
        return true;
    }

    // Когда отложенная задача станет готова, или пусто, если не отложена.
    [[nodiscard]] std::optional<Timestamp> not_before(TaskId id) const {
        const auto it = index_.find(id);
        if (it == index_.end() || it->second.not_before == Timestamp{}) {
            return std::nullopt;
        }
        return it->second.not_before;
    }

    // Убрать из очереди все ожидающие задачи и вернуть их вызывающему.
    // Сильная гарантия: вектор под задачи выделяется до изменений; дальше
    // обмен деревьев (не бросает), перемещение задач (не бросает) и
    // удаление записей индекса (не бросает).
    [[nodiscard]] std::vector<T> purge(std::string_view queue) {
        Tasks& tasks = existing(queue);
        std::vector<T> removed;
        removed.reserve(tasks.size());
        Tasks taken;
        taken.swap(tasks);
        for (auto& [key, task] : taken) {
            index_.erase(task.id());
            removed.push_back(std::move(task));
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
    [[nodiscard]] std::optional<std::reference_wrapper<T>> find(TaskId id) {
        const auto it = index_.find(id);
        if (it == index_.end()) {
            return std::nullopt;
        }
        return std::ref(it->second.position->second);
    }

    [[nodiscard]] std::optional<std::reference_wrapper<const T>> find(TaskId id) const {
        const auto it = index_.find(id);
        if (it == index_.end()) {
            return std::nullopt;
        }
        return std::cref(it->second.position->second);
    }

    // Имя очереди, в которой ждёт задача, — для STATUS.
    [[nodiscard]] std::optional<std::string> queue_of(TaskId id) const {
        const auto it = index_.find(id);
        if (it == index_.end()) {
            return std::nullopt;
        }
        return std::string(it->second.queue);
    }

    [[nodiscard]] std::size_t queue_count() const noexcept { return queues_.size(); }

    // Размер существующей очереди; отсутствующая — UnknownQueueError.
    template <NameLike N>
    [[nodiscard]] std::size_t size(const N& queue) const {
        return existing(name_view(queue)).size();
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
        for (const auto& [name, tasks] : queues_) {
            fn(name, QueueView(tasks));
        }
    }

private:
    // Следующая задача к выдаче: первая в порядке ключей среди готовых.
    // Отложенная задача пропускается, её ключ не важен, пока не наступило её
    // время. Пустая очередь или одни отложенные — end().
    template <class Tree>
    [[nodiscard]] auto next_in(Tree& tasks, Timestamp now) const {
        return std::ranges::find_if(tasks, [this, now](const auto& entry) {
            const auto it = index_.find(entry.second.id());
            return it == index_.end() || it->second.not_before <= now;
        });
    }

    [[nodiscard]] static typename Tasks::iterator insert(Tasks& tasks, T task) {
        TaskKey key = task.key();
        return tasks.emplace(std::move(key), std::move(task)).first;
    }

    void add_to_index(std::string_view queue, typename Tasks::iterator position) {
        const auto [it, inserted] = index_.try_emplace(position->second.id(), Location{queue, position});
        if (!inserted) {
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
    std::unordered_map<TaskId, Location> index_;
    std::uint64_t seq_ = 0;   // последний выданный номер постановки
};

using TaskStore = BasicTaskStore<Task>;
using Queue = TaskStore::Queue;

}  // namespace taskq
