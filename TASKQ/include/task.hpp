#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

using TaskId = std::uint64_t;
using Timestamp = std::chrono::system_clock::time_point;

struct TextPayload    { std::string command; };
struct NumericPayload { std::int64_t value = 0; };
struct BinaryPayload  { std::string bytes; };

inline bool operator==(const TextPayload& a, const TextPayload& b) { return a.command == b.command; }
inline bool operator==(const NumericPayload& a, const NumericPayload& b) { return a.value == b.value; }
inline bool operator==(const BinaryPayload& a, const BinaryPayload& b) { return a.bytes == b.bytes; }

using Payload = std::variant<TextPayload, NumericPayload, BinaryPayload>;

class Task {
public:
    Task() = default;

    Task(const TaskId id, Payload payload, const int priority = 0, const Timestamp at = Timestamp{})
        : id_(id), payload_(std::move(payload)), priority_(priority), at_(at) {}

    Task(const TaskId id, std::string command, const int priority = 0, const Timestamp at = Timestamp{})
        : Task(id, Payload{TextPayload{std::move(command)}}, priority, at) {}

    [[nodiscard]] TaskId id() const noexcept { return id_; }
    [[nodiscard]] const Payload& payload() const noexcept { return payload_; }
    [[nodiscard]] int priority() const noexcept { return priority_; }
    [[nodiscard]] Timestamp created_at() const noexcept { return at_; }

    [[nodiscard]] std::string_view type_name() const noexcept;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::size_t size() const noexcept;

    template <class T> [[nodiscard]] const T* get_if() const noexcept { return std::get_if<T>(&payload_); }
    template <class T> [[nodiscard]] T* get_if() noexcept { return std::get_if<T>(&payload_); }
    template <class T> [[nodiscard]] bool holds() const noexcept { return std::holds_alternative<T>(payload_); }

private:
    TaskId id_ = 0;
    Payload payload_;
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