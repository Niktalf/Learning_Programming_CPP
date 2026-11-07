#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <generator>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
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
// поставленная. Очередь остаётся вектором в порядке постановки, и следующая
// задача ищется линейно — min_element по ключу. На этапе 7 вектор сменит
// приоритетная очередь, и ключ станет её порядком.
template <QueuedTask T>
class BasicTaskStore {
public:
    using Queue = std::vector<T>;

    bool create_queue(std::string queue) {
        // try_emplace из C++17: значение конструируется только при вставке.
        return queues_.try_emplace(std::move(queue)).second;
    }

    // Этап 3: очередь заводит только CREATE. Постановка в несуществующую
    // очередь — UnknownQueueError: опечатка в имени иначе уводила бы задачи
    // туда, куда никто не смотрит.
    void submit(std::string_view queue, T task) {
        Queue& tasks = existing(queue);
        task.set_sequence(seq_ + 1);
        tasks.push_back(std::move(task));
        ++seq_;   // после вставки: бросок push_back номер не тратит
    }

    // Пакетная постановка с сильной гарантией: либо поставлены все задачи,
    // либо ни одной.
    //
    // Это copy-and-swap, но точечный: копируются не все очереди, а только
    // те, которых касается пакет. В копии дописываются задачи — здесь
    // бросают и копирования, и выделение памяти, но меняются только копии.
    // Затем каждая копия меняется местами с оригиналом. swap у std::vector —
    // noexcept, поэтому фиксация бросить не может, и это точка невозврата.
    //
    // Полная копия всех очередей дала бы ту же гарантию ценой копирования
    // всего хранилища на каждый пакет. Сравнение — в docs/errors.md.
    void submit_batch(const std::vector<std::pair<std::string_view, T>>& items) {
        // Ключ — представление имени из самого хранилища: ключи std::map не
        // перемещаются, пока элемент существует, и представление не повиснет.
        std::map<std::string_view, Queue> next;
        std::uint64_t seq = seq_;
        for (const auto& [queue, task] : items) {
            const auto it = queues_.find(queue);
            if (it == queues_.end()) {
                throw UnknownQueueError(std::string(queue));
            }
            const auto [slot, first_time] = next.try_emplace(it->first);
            if (first_time) {
                slot->second = it->second;   // копия очереди: оригинал не тронут
            }
            slot->second.push_back(task);    // копия задачи: оригинал не тронут
            slot->second.back().set_sequence(++seq);
        }

        // Точка невозврата: дальше только обмены, ни один не бросает.
        for (auto& [queue, tasks] : next) {
            queues_.find(queue)->second.swap(tasks);
        }
        seq_ = seq;
    }

    // Этап 2: вместо сырого указателя — std::optional. Этап 3 развёл два
    // случая, которые этап 2 сливал: отсутствующая очередь — UnknownQueueError,
    // а пустой optional означает ровно одно — очередь пуста. Пустая очередь —
    // штатный ответ, а не ошибка: воркер спрашивает, работы нет.
    //
    // Возвращается копия задачи, а не ссылка: std::optional<T&> стандартом не
    // предусмотрен.
    [[nodiscard]] std::optional<T> peek(std::string_view queue) const {
        const Queue& tasks = existing(queue);
        if (tasks.empty()) {
            return std::nullopt;
        }
        return *next_in(tasks);
    }

    // Номер первой задачи без копирования самой задачи. Нужен POLL, чтобы
    // подготовить место под задачу до того, как забрать её из очереди.
    [[nodiscard]] std::optional<TaskId> front_id(std::string_view queue) const {
        const Queue& tasks = existing(queue);
        if (tasks.empty()) {
            return std::nullopt;
        }
        return next_in(tasks)->id();
    }

    // Задача возвращается перемещением, без копии. Если перемещение T не
    // бросает, не бросает и take на непустой очереди: erase в начале вектора
    // лишь сдвигает элементы перемещением.
    [[nodiscard]] std::optional<T> take(std::string_view queue) {
        Queue& tasks = existing(queue);
        if (tasks.empty()) {
            return std::nullopt;
        }
        const auto next = next_in(tasks);
        T taken = std::move(*next);
        tasks.erase(next);
        return taken;
    }

    // --- Этап 5: LIST через ranges --------------------------------------
    //
    // Задачи очереди с приоритетом не ниже заданного, удовлетворяющие
    // предикату, — не больше limit, в порядке постановки. Конвейер:
    //
    //     filter(приоритет) | filter(предикат) | take
    //
    // Ничего не материализуется до take: на очереди из тысячи задач при
    // limit 10 предикаты вызываются немногим больше десяти раз. Тест
    // List.PipelineIsLazy проверяет это счётчиком.
    //
    // Возвращаются ссылки внутрь очереди: пока результат жив, очередь нельзя
    // менять — тот же урок, что со string_view на этапе 2.
    template <NameLike N, std::predicate<const T&> Predicate>
    [[nodiscard]] std::vector<std::reference_wrapper<const T>> tasks_where(const N& queue,
                                                                           int min_priority,
                                                                           Predicate predicate,
                                                                           std::size_t limit) const {
        const Queue& tasks = existing(name_view(queue));
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

    // Звёздочка этапа 5: задачи очереди генератором, в порядке постановки.
    // Корутина отдаёт задачи по одной и приостанавливается между ними. Если
    // обход прерван, кадр корутины уничтожается в точке приостановки, и её
    // локальные объекты разрушаются как положено: RAII работает и внутри
    // корутины. Плата — кадр корутины в куче.
    //
    // Генератор смотрит в очередь: обходить его можно, только пока она не
    // меняется. Несуществующая очередь — пустая последовательность.
    [[nodiscard]] std::generator<const T&> tasks_lazily(std::string queue) const {
        const auto it = queues_.find(queue);
        if (it == queues_.end()) {
            co_return;
        }
        for (const T& task : it->second) {
            co_yield task;
        }
    }

    // Этап 5: имя очереди — любой NameLike: строка, string_view, литерал.
    template <NameLike N>
    [[nodiscard]] bool contains(const N& queue) const {
        return queues_.find(name_view(queue)) != queues_.end();
    }

    // Поиск ожидающей задачи по номеру для TYPE и BUMP. Линейный перебор всех
    // очередей: на этапе 8 для этого появится индекс по номеру.
    [[nodiscard]] std::optional<std::reference_wrapper<T>> find(TaskId id) {
        for (auto& [name, tasks] : queues_) {
            for (T& task : tasks) {
                if (task.id() == id) {
                    return std::ref(task);
                }
            }
        }
        return std::nullopt;
    }

    // Имя очереди, в которой ждёт задача, — для STATUS.
    [[nodiscard]] std::optional<std::string> queue_of(TaskId id) const {
        for (const auto& [name, tasks] : queues_) {
            for (const T& task : tasks) {
                if (task.id() == id) {
                    return name;
                }
            }
        }
        return std::nullopt;
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
    // Structured bindings из C++17: пара раскладывается на имена.
    template <class F>
    void for_each_queue(F fn) const {
        for (const auto& [name, tasks] : queues_) {
            fn(name, tasks);
        }
    }

private:
    // Следующая задача по ключу выборки. Линейный поиск — сознательно: на
    // этапе 7 его заменит приоритетная очередь. Очередь не пуста: проверяет
    // вызывающий.
    template <class Tasks>
    [[nodiscard]] static auto next_in(Tasks& tasks) {
        return std::ranges::min_element(tasks, {}, [](const T& task) { return task.key(); });
    }

    // Одна точка, где отсутствие очереди превращается в исключение.
    [[nodiscard]] Queue& existing(std::string_view queue) {
        const auto it = queues_.find(queue);
        if (it == queues_.end()) {
            throw UnknownQueueError(std::string(queue));
        }
        return it->second;
    }

    [[nodiscard]] const Queue& existing(std::string_view queue) const {
        const auto it = queues_.find(queue);
        if (it == queues_.end()) {
            throw UnknownQueueError(std::string(queue));
        }
        return it->second;
    }

    // std::less<> включает разнородный поиск: методы принимают string_view.
    using Map = std::map<std::string, Queue, std::less<>>;
    Map queues_;
    std::uint64_t seq_ = 0;   // последний выданный номер постановки
};

using TaskStore = BasicTaskStore<Task>;
using Queue = TaskStore::Queue;

}  // namespace taskq
