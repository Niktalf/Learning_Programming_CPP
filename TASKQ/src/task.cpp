#include "task.hpp"

#include <sstream>
#include <type_traits>

#include "overloaded.hpp"

static_assert(std::is_nothrow_move_constructible_v<Task>, "Task must be move-constructible without exceptions");
static_assert(std::is_nothrow_move_assignable_v<Task>, "Task must be assignable via move without exceptions");
static_assert(std::is_copy_constructible_v<Task>, "Task must be copy-constructible");
static_assert(std::is_nothrow_destructible_v<Task>, "Task must be destructible without exceptions");

std::string_view Task::type_name() const noexcept {
    return std::visit(overloaded{
        [](const TextPayload&) -> std::string_view { return "text"; },
        [](const NumericPayload&) -> std::string_view { return "number"; },
        [](const BinaryPayload&) -> std::string_view { return "binary"; },
    }, payload_);
}

std::string Task::to_string() const {
    return std::visit(overloaded{
        [](const TextPayload& p) { return p.command; },
        [](const NumericPayload& p) {
            std::ostringstream out;
            out << p.value;
            return out.str();
        },
        [](const BinaryPayload& p) {
            std::ostringstream out;
            out << '<' << p.bytes.size() << " bites>";
            return out.str();
        },
    }, payload_);
}

std::size_t Task::size() const noexcept {
    return std::visit(overloaded{
        [](const TextPayload& p) { return p.command.size(); },
        [](const NumericPayload&) { return sizeof(std::int64_t); },
        [](const BinaryPayload& p) { return p.bytes.size(); },
    }, payload_);
}