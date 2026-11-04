#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/task.hpp"

namespace taskq {

// Хранилище параметризовано типом задачи по одной практической причине:
// в тестах вместо Task подставляется инструментированный тип, считающий
// копирования и перемещения. Без этого утверждение «постановка временной
// задачи не копирует» проверить нечем.
template <class T>
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
        existing(queue).push_back(std::move(task));
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
        }

        // Точка невозврата: дальше только обмены, ни один не бросает.
        for (auto& [queue, tasks] : next) {
            queues_.find(queue)->second.swap(tasks);
        }
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
        return tasks.front();
    }

    // Номер первой задачи без копирования самой задачи. Нужен POLL, чтобы
    // подготовить место под задачу до того, как забрать её из очереди.
    [[nodiscard]] std::optional<TaskId> front_id(std::string_view queue) const {
        const Queue& tasks = existing(queue);
        if (tasks.empty()) {
            return std::nullopt;
        }
        return tasks.front().id();
    }

    // Задача возвращается перемещением, без копии. Если перемещение T не
    // бросает, не бросает и take на непустой очереди: erase в начале вектора
    // лишь сдвигает элементы перемещением.
    [[nodiscard]] std::optional<T> take(std::string_view queue) {
        Queue& tasks = existing(queue);
        if (tasks.empty()) {
            return std::nullopt;
        }
        T taken = std::move(tasks.front());
        tasks.erase(tasks.begin());
        return taken;
    }

    [[nodiscard]] bool contains(std::string_view queue) const {
        return queues_.find(queue) != queues_.end();
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
    [[nodiscard]] std::size_t size(std::string_view queue) const {
        return existing(queue).size();
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
};

using TaskStore = BasicTaskStore<Task>;
using Queue = TaskStore::Queue;

}  // namespace taskq
