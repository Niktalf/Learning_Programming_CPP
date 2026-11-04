#pragma once

#include <cstddef>
#include <map>
#include <string>
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
        // try_emplace появится в C++17; пока обходимся поиском и вставкой.
        if (queues_.find(queue) != queues_.end()) {
            return false;
        }
        queues_.insert(std::make_pair(std::move(queue), Queue{}));
        return true;
    }

    // Постановка в несуществующую очередь заводит её. На этапе 3 это станет
    // предметом отдельного решения: возможно, неизвестная очередь должна
    // быть ошибкой, потому что опечатка в имени приводит к тому, что задачи
    // уходят туда, куда никто не смотрит.
    void submit(std::string queue, T task) {
        typename Map::iterator it = queues_.find(queue);
        if (it == queues_.end()) {
            it = queues_.insert(std::make_pair(std::move(queue), Queue{})).first;
        }
        it->second.push_back(std::move(task));
    }

    // Возврат сырого указателя на этом этапе намеренный, и он хуже, чем
    // кажется: nullptr означает сразу две разные вещи — «очереди нет» и
    // «очередь пуста». Первое обычно ошибка клиента, второе штатное
    // ожидание. На этапе 2 появится std::optional, а на этапе 3 эти случаи
    // придётся развести окончательно.
    const T* peek(const std::string& queue) const noexcept {
        typename Map::const_iterator it = queues_.find(queue);
        if (it == queues_.end() || it->second.empty()) {
            return nullptr;
        }
        return &it->second.front();
    }

    // Выходной параметр — идиома, предшествующая std::optional. Оставлена
    // намеренно: на этапе 2 будет видно, насколько optional читается лучше.
    bool take(const std::string& queue, T& out) {
        typename Map::iterator it = queues_.find(queue);
        if (it == queues_.end() || it->second.empty()) {
            return false;
        }
        out = std::move(it->second.front());
        it->second.erase(it->second.begin());
        return true;
    }

    std::size_t queue_count() const noexcept { return queues_.size(); }

    std::size_t size(const std::string& queue) const noexcept {
        typename Map::const_iterator it = queues_.find(queue);
        return it == queues_.end() ? 0 : it->second.size();
    }

    bool empty() const noexcept { return queues_.empty(); }

    // Функтор принимается по значению — как это делают алгоритмы стандартной
    // библиотеки. Именно поэтому лямбда с захватом по значению и mutable не
    // выносит изменения наружу: меняется копия.
    template <class F>
    void for_each_queue(F fn) const {
        for (const auto& entry : queues_) {
            fn(entry.first, entry.second);
        }
    }

private:
    using Map = std::map<std::string, Queue>;
    Map queues_;
};

using TaskStore = BasicTaskStore<Task>;

}  // namespace taskq
