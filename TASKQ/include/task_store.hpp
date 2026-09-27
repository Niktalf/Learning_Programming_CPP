#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include "task.hpp"

template <class T>
class BasicTaskStore {
public:
    using Queue = std::vector<T>;

    bool create_queue(std::string queue) {
        return queues_.try_emplace(std::move(queue)).second;
    }

    void submit(std::string queue, T task) {
        queues_[std::move(queue)].push_back(std::move(task));
    }

    [[nodiscard]] std::optional<T> peek(const std::string_view queue) const {
        typename Map::const_iterator it = queues_.find(queue);
        if (it == queues_.end() || it->second.empty()) {
            return std::nullopt;
        }
        return it->second.front();
    }

    [[nodiscard]] std::optional<T> take(const std::string_view queue) {
        typename Map::iterator it = queues_.find(queue);
        if (it == queues_.end() || it->second.empty()) {
            return std::nullopt;
        }
        T taken = std::move(it->second.front());
        it->second.erase(it->second.begin());
        return taken;
    }

    [[nodiscard]] T* front(std::string_view queue) noexcept {
        typename Map::iterator it = queues_.find(queue);
        if (it == queues_.end() || it->second.empty()) {
            return nullptr;
        }
        return &it->second.front();
    }

    [[nodiscard]] bool contains(std::string_view queue) const noexcept {
        return queues_.find(queue) != queues_.end();
    }

    [[nodiscard]] std::size_t queue_count() const noexcept { return queues_.size(); }

    [[nodiscard]] std::size_t size(std::string_view queue) const noexcept {
        typename Map::const_iterator it = queues_.find(queue);
        return it == queues_.end() ? 0 : it->second.size();
    }

    [[nodiscard]] bool empty() const noexcept { return queues_.empty(); }

    template <class F>
    void for_each_queue(F fn) const {
        for (const auto& [name, tasks] : queues_) {
            fn(name, tasks);
        }
    }

private:
    using Map = std::map<std::string, Queue, std::less<>>;
    Map queues_;
};

using TaskStore = BasicTaskStore<Task>;
using Queue = TaskStore::Queue;