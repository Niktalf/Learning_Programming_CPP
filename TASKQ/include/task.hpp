#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "ipayload.hpp"
#include "state.hpp"
#include "worker.hpp"

using TaskId = std::uint64_t;
using Timestamp = std::chrono::system_clock::time_point;

class Task {
public:
    Task(const TaskId id, std::unique_ptr<IPayload> payload, const int priority = 0, const Timestamp at = Timestamp{})
        : id_(id), payload_(std::move(payload)), priority_(priority), at_(at) {}

    Task(const TaskId id, std::string command, const int priority = 0, const Timestamp at = Timestamp{})
        : Task(id, std::make_unique<TextPayload>(std::move(command)), priority, at) {}

    Task(const Task& other) :   id_(other.id_), payload_(other.payload_->clone()), priority_(other.priority_),
                                at_(other.at_), state_(other.state_), holder_(other.holder_) {}

    Task& operator=(const Task& other) {
        Task copy(other);
        swap(*this, copy);
        return *this;
    }

    Task(Task&&) noexcept = default;
    Task& operator=(Task&&) noexcept = default;
    ~Task() = default;

    friend void swap(Task& a, Task& b) noexcept {
        std::swap(a.id_, b.id_);
        a.payload_.swap(b.payload_);
        std::swap(a.priority_, b.priority_);
        std::swap(a.at_, b.at_);
        std::swap(a.state_, b.state_);
        a.holder_.swap(b.holder_);
    }

    [[nodiscard]] TaskId id() const noexcept { return id_; }
    [[nodiscard]] int priority() const noexcept { return priority_; }
    [[nodiscard]] Timestamp created_at() const noexcept { return at_; }
    [[nodiscard]] State state() const noexcept { return state_; }
    void set_state(const State state) noexcept { state_ = state; }

    [[nodiscard]] std::string_view type_name() const noexcept { return payload_->type_name(); }
    [[nodiscard]] std::string to_string() const { return payload_->to_string(); }
    [[nodiscard]] std::size_t size() const noexcept { return payload_->size(); }

    template <class T> [[nodiscard]] const T* as() const noexcept { return dynamic_cast<const T*>(payload_.get()); }
    template <class T> [[nodiscard]] T* as() noexcept { return dynamic_cast<T*>(payload_.get()); }

    void set_holder(const std::shared_ptr<Worker>& worker) noexcept { holder_ = worker; }
    void clear_holder() noexcept { holder_.reset(); }

    [[nodiscard]] std::shared_ptr<Worker> holder() const noexcept { return holder_.lock(); }

    [[nodiscard]] bool abandoned() const noexcept {
        return state_ == State::Leased && holder_.expired();
    }

private:
    TaskId id_ = 0;
    std::unique_ptr<IPayload> payload_;
    int priority_ = 0;
    Timestamp at_ = Timestamp{};
    State state_ = State::Pending;
    std::weak_ptr<Worker> holder_;
};
