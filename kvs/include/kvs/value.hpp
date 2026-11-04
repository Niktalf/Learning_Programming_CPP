#pragma once

#include <string>
#include <utility>

namespace kvs {

// Правило нуля во плоти: ни одна из пяти специальных функций здесь не
// объявлена. Компилятор сгенерирует все пять сам, и они будут корректны,
// потому что единственное поле — std::string — само умеет корректно
// копироваться, перемещаться и разрушаться.
//
// Конструкторы правилу нуля не мешают. Его ломает объявление деструктора
// или любой из копирующих и перемещающих операций: объявленный деструктор
// подавляет генерацию перемещающих, и там, где ожидалось перемещение,
// начнётся копирование.
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

// Сравнение написано руками: генерация операторов по умолчанию появится
// только в C++20, вместе с оператором <=>. Пока пишем сами и следим, чтобы
// != был выражен через ==, а не продублирован.
inline bool operator==(const Value& lhs, const Value& rhs) noexcept {
    return lhs.str() == rhs.str();
}

inline bool operator!=(const Value& lhs, const Value& rhs) noexcept {
    return !(lhs == rhs);
}

}  // namespace kvs
