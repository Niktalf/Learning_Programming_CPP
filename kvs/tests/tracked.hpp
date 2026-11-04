#pragma once

#include <cstddef>
#include <string>
#include <utility>

namespace kvs {
namespace testing {

// Счётчики хранятся в статических переменных внутри функций: static-поля
// класса в C++14 потребовали бы отдельного определения в .cpp, а inline-поля
// появятся только в C++17.
inline int& copy_counter() {
    static int count = 0;
    return count;
}

inline int& move_counter() {
    static int count = 0;
    return count;
}

// Инструментированный тип-шпион: считает копирования и перемещения.
// Живёт только в тестах, поэтому Value остаётся написанным по правилу нуля.
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
    std::size_t size() const noexcept { return data_.size(); }

    static void reset() noexcept { copy_counter() = 0; move_counter() = 0; }
    static int copies() noexcept { return copy_counter(); }
    static int moves() noexcept { return move_counter(); }

private:
    std::string data_;
};

}  // namespace testing
}  // namespace kvs
