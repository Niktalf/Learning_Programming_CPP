#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace chat {

// Корень иерархии ошибок проекта. Наследуемся от std::runtime_error, а не от
// std::exception напрямую: runtime_error уже умеет хранить сообщение.
//
// Главное правило: контекст ошибки лежит в полях, а не только в тексте
// сообщения. На этапе 16 по типу и полям выберется код ответа HTTP, и
// разбирать для этого строку сообщения недопустимо.
class ChatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Строка не разобрана. Несёт причину и исходный ввод — то, чего не хватало
// пустому std::optional на этапе 2. На этапе 16 станет кодом 400.
class ParseError : public ChatError {
public:
    ParseError(std::string reason, std::string input);

    [[nodiscard]] const std::string& reason() const noexcept { return reason_; }
    [[nodiscard]] const std::string& input() const noexcept { return input_; }

private:
    std::string reason_;
    std::string input_;
};

// Комнаты нет. На этапе 16 станет кодом 404.
class UnknownRoomError : public ChatError {
public:
    explicit UnknownRoomError(std::string room);

    [[nodiscard]] const std::string& room() const noexcept { return room_; }

private:
    std::string room_;
};

// Писать в комнату, менять тему и выходить может только участник.
// На этапе 16 станет кодом 403.
class NotMemberError : public ChatError {
public:
    NotMemberError(std::string room, std::string nick);

    [[nodiscard]] const std::string& room() const noexcept { return room_; }
    [[nodiscard]] const std::string& nick() const noexcept { return nick_; }

private:
    std::string room_;
    std::string nick_;
};

// Ник уже занят. На этапе 16 станет кодом 409. Пока в консольном клиенте один
// пользователь, ошибка видна в тестах с двумя сессиями; с этапа 15, когда
// подключатся несколько клиентов, она станет повседневной.
class NicknameTakenError : public ChatError {
public:
    explicit NicknameTakenError(std::string nick);

    [[nodiscard]] const std::string& nick() const noexcept { return nick_; }

private:
    std::string nick_;
};

// Ник не зарегистрирован. Этап 4: в комнату входит объект пользователя из
// реестра, а не строка, и войти может только тот, кто в реестре есть.
// На этапе 16 станет кодом 404.
class UnknownUserError : public ChatError {
public:
    explicit UnknownUserError(std::string nick);

    [[nodiscard]] const std::string& nick() const noexcept { return nick_; }

private:
    std::string nick_;
};

// Сообщения с таким номером в комнате нет. На этапе 16 станет кодом 404.
class MessageNotFoundError : public ChatError {
public:
    MessageNotFoundError(std::string room, std::size_t index);

    [[nodiscard]] const std::string& room() const noexcept { return room_; }
    [[nodiscard]] std::size_t index() const noexcept { return index_; }

private:
    std::string room_;
    std::size_t index_;
};

// Операция неприменима к виду сообщения: /react к тексту. На этапе 16 — 409.
class TypeMismatchError : public ChatError {
public:
    TypeMismatchError(std::string room, std::size_t index, std::string expected,
                      std::string actual);

    [[nodiscard]] const std::string& room() const noexcept { return room_; }
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] const std::string& expected() const noexcept { return expected_; }
    [[nodiscard]] const std::string& actual() const noexcept { return actual_; }

private:
    std::string room_;
    std::size_t index_;
    std::string expected_;
    std::string actual_;
};

// Счётчик реакции вышел бы за пределы int64. Этого класса нет в постановке:
// его нашёл фаззер из домашнего задания. До него /react с шагом около
// INT64_MAX давал переполнение знакового целого — неопределённое поведение.
// На этапе 16 станет кодом 409.
class CounterOverflowError : public ChatError {
public:
    CounterOverflowError(std::string room, std::size_t index, std::int64_t current,
                         std::int64_t by);

    [[nodiscard]] const std::string& room() const noexcept { return room_; }
    [[nodiscard]] std::size_t index() const noexcept { return index_; }

private:
    std::string room_;
    std::size_t index_;
};

// Звёздочка этапа 3: разворот цепочки, собранной std::throw_with_nested.
//
// Это часть границы, а не ядра: обе функции перехватывают исключения, чтобы
// заглянуть внутрь, и вызываются только из handle и из тестов.

// Самое внутреннее исключение цепочки — исходная причина.
[[nodiscard]] std::exception_ptr root_cause(std::exception_ptr error);

// Тексты внешних звеньев, от внешнего к внутреннему, без самой причины:
// «while executing /msg #general» и так далее.
[[nodiscard]] std::vector<std::string> context_of(std::exception_ptr error);

}  // namespace chat
