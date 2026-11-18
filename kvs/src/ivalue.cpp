#include "kvs/ivalue.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    // Обход итераторами, а не по индексу: items_[i] у deque — O(1), но с
    // двумя обращениями к памяти (карта блоков, затем блок); итератор идёт
    // по блоку подряд.
    std::string text = "[";
    const char* separator = "";
    for (const std::string& item : items_) {
        text += separator;
        text += item;
        separator = ", ";
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

void ListValue::push_front(std::string item) {
    const std::size_t added = item.size();
    items_.push_front(std::move(item));
    bytes_ += added;   // после вставки: если push_front бросил, учёт не тронут
}

void ListValue::push_back(std::string item) {
    const std::size_t added = item.size();
    items_.push_back(std::move(item));
    bytes_ += added;
}

// Снятие не бросает после того, как элемент перемещён наружу: pop_front у
// deque не бросает, вычитание — тоже. Возврат по значению перемещением.
std::string ListValue::pop_front() {
    std::string item = std::move(items_.front());
    items_.pop_front();
    bytes_ -= item.size();
    return item;
}

std::string ListValue::pop_back() {
    std::string item = std::move(items_.back());
    items_.pop_back();
    bytes_ -= item.size();
    return item;
}

std::vector<std::string> ListValue::range(std::int64_t start, std::int64_t stop) const {
    const auto length = static_cast<std::int64_t>(items_.size());
    // Отрицательный индекс — от конца. После сдвига всё ещё отрицательный —
    // до начала списка.
    const std::int64_t first = std::max<std::int64_t>(start < 0 ? start + length : start, 0);
    const std::int64_t last = std::min<std::int64_t>(stop < 0 ? stop + length : stop, length - 1);
    if (first > last) {
        return {};
    }
    // Итераторы deque — произвольного доступа: begin() + first — O(1), а не
    // проход от начала, как был бы у list.
    const auto from = items_.begin() + first;
    return std::vector<std::string>(from, from + (last - first + 1));
}

}  // namespace kvs
