#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "task.hpp"


template <class T>
class BasicTaskStore {
public:
    using Queue = std::vector<T>;

    bool create_queue(std::string queue) {
        if (queues_.find(queue) != queues_.end()) {
            return false;
        }
        queues_.insert(std::make_pair(std::move(queue), Queue{}));
        return true;
    }

    void submit(std::string queue, T task) {
        typename Map::iterator it = queues_.find(queue);
        if (it == queues_.end()) {
            it = queues_.insert(std::make_pair(std::move(queue), Queue{})).first;
        }
        it->second.push_back(std::move(task));
    }

    const T* peek(const std::string& queue) const noexcept {
        typename Map::const_iterator it = queues_.find(queue);
        if (it == queues_.end() || it->second.empty()) {
            return nullptr;
        }
        return &it->second.front();
    }

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
