#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

using TaskId = std::uint64_t;
using Timestamp = std::chrono::system_clock::time_point;


class Task {
public:
    Task() = default;

    Task(TaskId id, std::string payload, int priority = 0, Timestamp at = Timestamp{})
        : id_(id), payload_(std::move(payload)), priority_(priority), at_(at) {}

    TaskId id() const noexcept { return id_; }
    const std::string& payload() const noexcept { return payload_; }
    int priority() const noexcept { return priority_; }
    Timestamp created_at() const noexcept { return at_; }
    std::size_t size() const noexcept { return payload_.size(); }

private:
    TaskId id_ = 0;
    std::string payload_;
    int priority_ = 0;
    Timestamp at_ = Timestamp{};
};

inline bool operator==(const Task& lhs, const Task& rhs) noexcept {
    return lhs.id() == rhs.id() && lhs.payload() == rhs.payload() &&
        lhs.priority() == rhs.priority() && lhs.created_at() == rhs.created_at();
}

inline bool operator!=(const Task& lhs, const Task& rhs) noexcept {
    return !(lhs == rhs);
}
