#include "value.hpp"

#include <sstream>
#include <type_traits>

#include "overloaded.hpp"

static_assert(std::is_nothrow_move_constructible_v<Value>, "Value must be moved without exceptions");
static_assert(std::is_nothrow_move_assignable_v<Value>, "Value must be assigned by movement without exceptions");
static_assert(std::is_copy_constructible_v<Value>, "Value must be copied");
static_assert(std::is_nothrow_destructible_v<Value>, "Value must be destroyed without exception");

std::string_view Value::type_name() const noexcept {
    return std::visit(overloaded{
        [](const std::string&) -> std::string_view { return "string"; },
        [](std::int64_t) -> std::string_view { return "int"; },
        [](double) -> std::string_view { return "float"; },
    }, data_);
}

std::string Value::to_string() const {
    return std::visit(overloaded{
        [](const std::string& s) { return s; },
            [](const std::int64_t i) {
                std::ostringstream out;
                out << i;
                return out.str();
        },
        [](const double d) {
            std::ostringstream out;
            out << d;
            return out.str();
        },
    }, data_);
}

std::size_t Value::size() const noexcept {
    if (const auto* s = std::get_if<std::string>(&data_)) {
        return s->size();
    }
    return sizeof(ValueData);
}