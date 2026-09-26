#pragma once

#include <string>
#include <utility>

class Value {
public:
    Value() = default;

    explicit Value(std::string data) noexcept : data_(std::move(data)) {}

    const std::string& str() const noexcept { return data_; }
    std::size_t size() const noexcept { return data_.size(); }
    bool empty() const noexcept { return data_.empty(); }

private:
    std::string data_;
};

inline bool operator==(const Value& lhs, const Value& rhs) noexcept {
    return lhs.str() == rhs.str();
}

inline bool operator!=(const Value& lhs, const Value& rhs) noexcept {
    return !(lhs == rhs);
}
