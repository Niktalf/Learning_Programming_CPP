#include "kvs/value.hpp"

#include <sstream>
#include <type_traits>

#include "kvs/overloaded.hpp"

namespace kvs {

// Переменные-шаблоны с суффиксом _v появились в C++17, здесь они уже уместны.
static_assert(std::is_nothrow_move_constructible_v<Value>,
              "Value must be nothrow move constructible");
static_assert(std::is_copy_constructible_v<Value>, "Value must be copy constructible");
static_assert(std::is_nothrow_destructible_v<Value>,
              "Value must be nothrow destructible");

std::string_view Value::type_name() const noexcept {
    return std::visit(overloaded{
                          [](const std::string&) -> std::string_view { return "string"; },
                          [](std::int64_t) -> std::string_view { return "int"; },
                          [](double) -> std::string_view { return "float"; },
                      },
                      data_);
}

std::string Value::to_string() const {
    // std::format появится в C++20, пока форматируем потоком.
    return std::visit(overloaded{
                          [](const std::string& s) { return s; },
                          [](std::int64_t i) {
                              std::ostringstream out;
                              out << i;
                              return out.str();
                          },
                          [](double d) {
                              std::ostringstream out;
                              out << d;
                              return out.str();
                          },
                      },
                      data_);
}

std::size_t Value::size() const noexcept {
    if (const auto* const text = std::get_if<std::string>(&data_)) {
        return text->size();
    }
    return sizeof(ValueData);
}

}  // namespace kvs
