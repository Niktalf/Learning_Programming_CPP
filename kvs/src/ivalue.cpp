#include "kvs/ivalue.hpp"

#include <format>
#include <memory>
#include <string>
#include <utility>

namespace kvs {

// clone всех наследников устроен одинаково: копирующий конструктор
// конкретного класса, результат — в unique_ptr на базу. make_unique, а не
// new: ни одного new в рабочем коде.

std::unique_ptr<IValue> StringValue::clone() const {
    return std::make_unique<StringValue>(*this);
}

// equals сначала проверяет динамический тип: строка "42" и целое 42 — разные
// значения.
bool StringValue::equals(const IValue& other) const noexcept {
    const auto* const same = dynamic_cast<const StringValue*>(&other);
    return same != nullptr && same->data_ == data_;
}

std::string IntValue::to_string() const {
    // Этап 5: стандарт C++23, std::format вместо строкового потока.
    return std::format("{}", data_);
}

std::unique_ptr<IValue> IntValue::clone() const {
    return std::make_unique<IntValue>(*this);
}

bool IntValue::equals(const IValue& other) const noexcept {
    const auto* const same = dynamic_cast<const IntValue*>(&other);
    return same != nullptr && same->data_ == data_;
}

std::string ListValue::to_string() const {
    std::string text = "[";
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (i != 0) {
            text += ", ";
        }
        text += items_[i];
    }
    text += ']';
    return text;
}

std::unique_ptr<IValue> ListValue::clone() const {
    return std::make_unique<ListValue>(*this);
}

bool ListValue::equals(const IValue& other) const noexcept {
    const auto* const same = dynamic_cast<const ListValue*>(&other);
    return same != nullptr && same->items_ == items_;
}

void ListValue::push_back(std::string item) {
    const std::size_t added = item.size();
    items_.push_back(std::move(item));
    bytes_ += added;   // после вставки: если push_back бросил, учёт не тронут
}

}  // namespace kvs
