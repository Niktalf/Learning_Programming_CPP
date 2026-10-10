#pragma once

#include <string>
#include <utility>

class Tracked {
public:
    Tracked() = default;
    explicit Tracked(std::string data) : data_(std::move(data)) {}

    Tracked(const Tracked& other) : data_(other.data_) { ++copy_counter_; }
    Tracked(Tracked&& other) noexcept : data_(std::move(other.data_)) { ++move_counter_; }

    Tracked& operator=(const Tracked& other) {
        if (this != &other) {
            data_ = other.data_;
            ++copy_counter_;
        }
        return *this;
    }

    Tracked& operator=(Tracked&& other) noexcept {
        data_ = std::move(other.data_);
        ++move_counter_;
        return *this;
    }

    ~Tracked() = default;

    [[nodiscard]] const std::string& str() const noexcept { return data_; }

    static void reset() noexcept { copy_counter_ = 0; move_counter_ = 0; }
    static int copies() noexcept { return copy_counter_; }
    static int moves() noexcept { return move_counter_; }

private:
    std::string data_;

    static inline int copy_counter_ = 0;
    static inline int move_counter_ = 0;
};
