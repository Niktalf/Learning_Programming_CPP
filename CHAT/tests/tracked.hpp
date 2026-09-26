#pragma once

#include <string>
#include <utility>

inline int& copy_counter() {
    static int count = 0;
    return count;
}

inline int& move_counter() {
    static int count = 0;
    return count;
}

class Tracked {
public:
    Tracked() = default;
    explicit Tracked(std::string data) : data_(std::move(data)) {}

    Tracked(const Tracked& other) : data_(other.data_) { ++copy_counter(); }
    Tracked(Tracked&& other) noexcept : data_(std::move(other.data_)) { ++move_counter(); }

    Tracked& operator=(const Tracked& other) {
        if (this != &other) {
            data_ = other.data_;
            ++copy_counter();
        }
        return *this;
    }

    Tracked& operator=(Tracked&& other) noexcept {
        data_ = std::move(other.data_);
        ++move_counter();
        return *this;
    }

    ~Tracked() = default;

    const std::string& str() const noexcept { return data_; }

    static void reset() noexcept { copy_counter() = 0; move_counter() = 0; }
    static int copies() noexcept { return copy_counter(); }
    static int moves() noexcept { return move_counter(); }

private:
    std::string data_;
};
