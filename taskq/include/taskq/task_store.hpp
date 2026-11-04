#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

    // Постановка в несуществующую очередь заводит её. На этапе 3 это станет
    // предметом отдельного решения: неизвестная очередь окажется ошибкой,
    // потому что опечатка в имени уводит задачи туда, куда никто не смотрит.
    void submit(std::string queue, T task) {
        queues_[std::move(queue)].push_back(std::move(task));
    }

    // Этап 2: вместо сырого указателя — std::optional. Пустое значение
    // по-прежнему сливает два разных случая: «очереди нет» и «очередь пуста».
    // Тип стал честнее — забыть о проверке нельзя, — но причины отказа он
    // не несёт. На этапе 3 их придётся развести исключением.
    //
    // Возвращается копия задачи, а не ссылка: std::optional<T&> стандартом не
    // предусмотрен. Для задачи с большим телом копия стоит заметно, и это
    // стоит помнить как цену optional, а не считать его бесплатным.
    [[nodiscard]] std::optional<T> peek(std::string_view queue) const {
        const auto it = queues_.find(queue);
        if (it == queues_.end() || it->second.empty()) {
            return std::nullopt;
        }
        return it->second.front();
    }

    // Выходной параметр этапа 1 заменён на optional: задача возвращается
    // перемещением, без копии и без «пустой» задачи для заполнения.
    [[nodiscard]] std::optional<T> take(std::string_view queue) {
        const auto it = queues_.find(queue);
        if (it == queues_.end() || it->second.empty()) {
            return std::nullopt;
        }
        T taken = std::move(it->second.front());
        it->second.erase(it->second.begin());
        return taken;
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

    [[nodiscard]] std::size_t size(std::string_view queue) const {
        const auto it = queues_.find(queue);
        return it == queues_.end() ? 0 : it->second.size();
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
    // std::less<> включает разнородный поиск: методы принимают string_view.
    using Map = std::map<std::string, Queue, std::less<>>;
    Map queues_;
};

using TaskStore = BasicTaskStore<Task>;
using Queue = TaskStore::Queue;

}  // namespace taskq
