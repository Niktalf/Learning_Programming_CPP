#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

namespace chat {

using Timestamp = std::chrono::system_clock::time_point;

// Правило нуля во плоти: ни одна из пяти специальных функций здесь не
// объявлена. Компилятор сгенерирует все пять сам, и они будут корректны,
// потому что все поля — строки и отметка времени — сами умеют корректно
// копироваться, перемещаться и разрушаться.
//
// Конструкторы правилу нуля не мешают. Его ломает объявление деструктора
// или любой из копирующих и перемещающих операций: объявленный деструктор
// подавляет генерацию перемещающих, и там, где ожидалось перемещение
// текста сообщения, начнётся копирование.
class Message {
public:
    Message() = default;

    Message(std::string author, std::string text, Timestamp at = Timestamp{})
        : author_(std::move(author)), text_(std::move(text)), at_(at) {}

    const std::string& author() const noexcept { return author_; }
    const std::string& text() const noexcept { return text_; }
    Timestamp at() const noexcept { return at_; }
    std::size_t size() const noexcept { return text_.size(); }

private:
    std::string author_;
    std::string text_;
    Timestamp at_ = Timestamp{};
};

// Сравнение написано руками: генерация операторов по умолчанию появится
// только в C++20, вместе с оператором <=>.
inline bool operator==(const Message& lhs, const Message& rhs) noexcept {
    return lhs.author() == rhs.author() && lhs.text() == rhs.text() &&
           lhs.at() == rhs.at();
}

inline bool operator!=(const Message& lhs, const Message& rhs) noexcept {
    return !(lhs == rhs);
}

}  // namespace chat
