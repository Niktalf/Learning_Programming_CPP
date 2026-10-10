#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "ivalue.hpp"

class Value {
public:
    explicit Value(std::string data) : impl_(std::make_unique<StringValue>(std::move(data))) {}
    explicit Value(const std::string_view data) : Value(std::string(data)) {}
    explicit Value(std::int64_t data) : impl_(std::make_unique<IntValue>(data)) {}
    explicit Value(double data) : impl_(std::make_unique<FloatValue>(data)) {}
    explicit Value(std::unique_ptr<IValue> impl) noexcept : impl_(std::move(impl)) {}

    Value(const Value& other) : impl_(other.impl_->clone()) {}

    Value& operator=(const Value& other) {
        Value copy(other);
        swap(*this, copy);
        return *this;
    }

    Value(Value&&) noexcept = default;
    Value& operator=(Value&&) noexcept = default;
    ~Value() = default;

    friend void swap(Value& a, Value& b) noexcept { a.impl_.swap(b.impl_); }

    [[nodiscard]] std::string_view type_name() const noexcept { return impl_->type_name(); }
    [[nodiscard]] std::string to_string() const { return impl_->to_string(); }
    [[nodiscard]] std::size_t size() const noexcept { return impl_->size(); }

    template <class T>
    [[nodiscard]] const T* as() const noexcept {
        return dynamic_cast<const T*>(impl_.get());
    }

    template <class T>
    [[nodiscard]] T* as() noexcept {
        return dynamic_cast<T*>(impl_.get());
    }

    friend bool operator==(const Value& a, const Value& b) noexcept {
        return a.impl_->equals(*b.impl_);
    }
    friend bool operator!=(const Value& a, const Value& b) noexcept { return !(a == b); }

private:
    std::unique_ptr<IValue> impl_;
};
