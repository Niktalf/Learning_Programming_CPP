#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

using ValueData = std::variant<std::string, std::int64_t, double>;

class Value {
public:
    Value() = default;

    explicit Value(std::string data) noexcept : data_(std::move(data)) {}
    explicit Value(const std::string_view data) : data_(std::string(data)) {}
    explicit Value(std::int64_t data) noexcept : data_(data) {}
    explicit Value(double data) noexcept : data_(data) {}

    [[nodiscard]] const ValueData& data() const noexcept { return data_; }

    [[nodiscard]] std::string_view type_name() const noexcept;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::size_t size() const noexcept;

    template <class T>
    [[nodiscard]] bool holds() const noexcept {
        return std::holds_alternative<T>(data_);
    }

    template <class T>
    [[nodiscard]] const T* get_if() const noexcept {
        return std::get_if<T>(&data_);
    }

private:
    ValueData data_;
};

inline bool operator==(const Value& lhs, const Value& rhs) noexcept {
    return lhs.data() == rhs.data();
}

inline bool operator!=(const Value& lhs, const Value& rhs) noexcept {
    return !(lhs == rhs);
}
