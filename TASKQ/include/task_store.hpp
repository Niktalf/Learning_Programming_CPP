#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "errors.hpp"
#include "state.hpp"
#include "task.hpp"
#include "worker.hpp"

template <class T>
class BasicTaskStore {
public:
    using Queue = std::vector<T>;

    bool create_queue(std::string queue) {
        return queues_.try_emplace(std::move(queue)).second;
    }

    void submit(std::string_view queue, T task) {
        require(queue).push_back(std::move(task));
    }

    void submit_batch(const std::vector<std::pair<std::string, T>>& items) {
        Map next = queues_;
        for (const auto& [queue, task] : items) {
            const auto it = next.find(queue);
            if (it == next.end()) {
                throw UnknownQueueError(queue);
            }
            it->second.push_back(task);
        }
        queues_.swap(next);
    }

    [[nodiscard]] std::optional<T> peek(const std::string_view queue) const {
        const Queue& tasks = require(queue);
        if (tasks.empty()) {
            return std::nullopt;
        }
        return tasks.front();
    }

    bool register_worker(std::string name) {
        if (workers_.find(name) != workers_.end()) {
            return false;
        }
        auto worker = std::make_shared<Worker>(name);
        workers_.emplace(std::move(name), std::move(worker));
        return true;
    }

    bool remove_worker(const std::string_view name) {
        const auto it = workers_.find(name);
        if (it == workers_.end()) {
            return false;
        }
        workers_.erase(it);
        return true;
    }

    [[nodiscard]] std::size_t worker_count() const noexcept { return workers_.size(); }

    [[nodiscard]] std::optional<T> lease(std::string_view queue, std::string_view worker_name) {
        const auto worker = workers_.find(worker_name);
        if (worker == workers_.end()) {
            throw UnknownWorkerError(std::string(worker_name));
        }
        for (T& task : require(queue)) {
            if (task.state() == State::Pending) {
                task.set_state(State::Leased);
                task.set_holder(worker->second);
                return task;
            }
        }
        return std::nullopt;
    }

    void transition(const std::uint64_t id, State to) {
        T& task = require_task(id);
        if (!allowed(task.state(), to)) {
            throw InvalidTransitionError(id, task.state(), to);
        }
        task.set_state(to);
        if (to != State::Leased) {
            task.clear_holder();
        }
    }

    [[nodiscard]] const T& find(const std::uint64_t id) const { return require_task(id); }
    [[nodiscard]] T& find(const std::uint64_t id) { return require_task(id); }

    [[nodiscard]] bool contains(std::string_view queue) const noexcept {
        return queues_.find(queue) != queues_.end();
    }

    [[nodiscard]] std::size_t queue_count() const noexcept { return queues_.size(); }
    [[nodiscard]] std::size_t size(std::string_view queue) const { return require(queue).size(); }
    [[nodiscard]] bool empty() const noexcept { return queues_.empty(); }

    template <class F>
    void for_each_queue(F fn) const {
        for (const auto& [name, tasks] : queues_) {
            fn(name, tasks);
        }
    }

    friend void swap(BasicTaskStore& a, BasicTaskStore& b) noexcept {
        a.queues_.swap(b.queues_);
        a.workers_.swap(b.workers_);
    }

private:
    using Map = std::map<std::string, Queue, std::less<>>;

    Queue& require(std::string_view queue) {
        const auto it = queues_.find(queue);
        if (it == queues_.end()) {
            throw UnknownQueueError(std::string(queue));
        }
        return it->second;
    }

    [[nodiscard]] const Queue& require(std::string_view queue) const {
        const auto it = queues_.find(queue);
        if (it == queues_.end()) {
            throw UnknownQueueError(std::string(queue));
        }
        return it->second;
    }

    template <class Self>
    static auto& find_task(Self& self, std::uint64_t id) {
        for (auto& [name, tasks] : self.queues_) {
            for (auto& task : tasks) {
                if (task.id() == id) {
                    return task;
                }
            }
        }
        throw UnknownTaskError(id);
    }

    T& require_task(std::uint64_t id) { return find_task(*this, id); }
    [[nodiscard]] const T& require_task(std::uint64_t id) const { return find_task(*this, id); }

    Map queues_;
    std::map<std::string, std::shared_ptr<Worker>, std::less<>> workers_;
};

using TaskStore = BasicTaskStore<Task>;
using Queue = TaskStore::Queue;
