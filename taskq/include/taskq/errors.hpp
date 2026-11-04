#pragma once

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "taskq/state.hpp"
#include "taskq/task.hpp"

namespace taskq {

// Корень иерархии ошибок проекта. Наследуемся от std::runtime_error, а не от
// std::exception напрямую: runtime_error уже умеет хранить сообщение.
//
// Главное правило: контекст ошибки лежит в полях, а не только в тексте
// сообщения. На этапе 16 по типу и полям выберется код ответа HTTP, и
// разбирать для этого строку сообщения недопустимо.
class TaskQError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Строка или тело задачи не разобраны. Несёт причину и исходный ввод — то,
// чего не хватало пустому std::optional на этапе 2. На этапе 16 — код 400.
class ParseError : public TaskQError {
public:
    ParseError(std::string reason, std::string input);

    [[nodiscard]] const std::string& reason() const noexcept { return reason_; }
    [[nodiscard]] const std::string& input() const noexcept { return input_; }

private:
    std::string reason_;
    std::string input_;
};

// Очереди нет. Этап 2 сливал этот случай с пустой очередью; теперь пустая
// очередь — обычный ответ, а отсутствующая — ошибка. На этапе 16 — код 404.
class UnknownQueueError : public TaskQError {
public:
    explicit UnknownQueueError(std::string queue);

    [[nodiscard]] const std::string& queue() const noexcept { return queue_; }

private:
    std::string queue_;
};

// Задачи с таким номером никогда не было. На этапе 16 — код 404.
class UnknownTaskError : public TaskQError {
public:
    explicit UnknownTaskError(TaskId id);

    [[nodiscard]] TaskId id() const noexcept { return id_; }

private:
    TaskId id_;
};

// Переход запрещён автоматом: ACK ожидающей задачи, повторный ACK. Задача
// существует, но сделать с ней это нельзя — поэтому 409, а не 404, и клиент
// обязан уметь их различать.
class InvalidTransitionError : public TaskQError {
public:
    InvalidTransitionError(TaskId id, State from, State to);

    [[nodiscard]] TaskId id() const noexcept { return id_; }
    [[nodiscard]] State from() const noexcept { return from_; }
    [[nodiscard]] State to() const noexcept { return to_; }

private:
    TaskId id_;
    State from_;
    State to_;
};

// Команда, не меняющая состояние, неприменима в текущем: BUMP выданной
// задачи, TYPE завершённой. Это не переход, поэтому отдельный класс.
// На этапе 16 — код 409.
class WrongStateError : public TaskQError {
public:
    WrongStateError(TaskId id, State actual, std::string operation);

    [[nodiscard]] TaskId id() const noexcept { return id_; }
    [[nodiscard]] State actual() const noexcept { return actual_; }
    [[nodiscard]] const std::string& operation() const noexcept { return operation_; }

private:
    TaskId id_;
    State actual_;
    std::string operation_;
};

// Операция неприменима к виду тела: BUMP текстовой задачи. На этапе 16 — 409.
class TypeMismatchError : public TaskQError {
public:
    TypeMismatchError(TaskId id, std::string expected, std::string actual);

    [[nodiscard]] TaskId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& expected() const noexcept { return expected_; }
    [[nodiscard]] const std::string& actual() const noexcept { return actual_; }

private:
    TaskId id_;
    std::string expected_;
    std::string actual_;
};

// BUMP вышел бы за пределы int64. Этого класса нет в постановке: его нашёл
// фаззер из домашнего задания. До него BUMP с шагом около INT64_MAX давал
// переполнение знакового целого — неопределённое поведение. Код 409.
class OverflowError : public TaskQError {
public:
    OverflowError(TaskId id, std::int64_t current, std::int64_t by);

    [[nodiscard]] TaskId id() const noexcept { return id_; }

private:
    TaskId id_;
};

// Звёздочка этапа 3: разворот цепочки, собранной std::throw_with_nested.
//
// Это часть границы, а не ядра: обе функции перехватывают исключения, чтобы
// заглянуть внутрь, и вызываются только из handle и из тестов.

// Самое внутреннее исключение цепочки — исходная причина.
[[nodiscard]] std::exception_ptr root_cause(std::exception_ptr error);

// Тексты внешних звеньев, от внешнего к внутреннему, без самой причины:
// «while executing ACK 7» и так далее.
[[nodiscard]] std::vector<std::string> context_of(std::exception_ptr error);

}  // namespace taskq
