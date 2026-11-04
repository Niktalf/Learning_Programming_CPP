#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace kvs {

// Корень иерархии ошибок проекта. Наследуемся от std::runtime_error, а не от
// std::exception напрямую: runtime_error уже умеет хранить сообщение, и нам
// не нужно писать собственный what().
//
// Главное правило: контекст ошибки лежит в полях, а не только в тексте
// сообщения. На этапе 16 по типу и полям надо будет выбрать код ответа HTTP,
// и разбирать для этого строку сообщения недопустимо.
class KvError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Строка не разобрана. Несёт причину и исходный ввод — то, чего не хватало
// пустому std::optional на этапе 2. На этапе 16 станет кодом 400.
class ParseError : public KvError {
public:
    ParseError(std::string reason, std::string input);

    [[nodiscard]] const std::string& reason() const noexcept { return reason_; }
    [[nodiscard]] const std::string& input() const noexcept { return input_; }

private:
    std::string reason_;
    std::string input_;
};

// Ключа нет. На этапе 16 станет кодом 404.
class KeyNotFoundError : public KvError {
public:
    explicit KeyNotFoundError(std::string key);

    [[nodiscard]] const std::string& key() const noexcept { return key_; }

private:
    std::string key_;
};

// Операция неприменима к типу значения. На этапе 16 станет кодом 409.
class TypeMismatchError : public KvError {
public:
    TypeMismatchError(std::string key, std::string expected, std::string actual);

    [[nodiscard]] const std::string& key() const noexcept { return key_; }
    [[nodiscard]] const std::string& expected() const noexcept { return expected_; }
    [[nodiscard]] const std::string& actual() const noexcept { return actual_; }

private:
    std::string key_;
    std::string expected_;
    std::string actual_;
};

// Записи не хватает памяти: хранилище работает с ограничением на объём, и
// новое значение его превысило. Ровно как ответ Redis «OOM command not allowed
// when used memory > maxmemory». Несёт ключ, сколько байт понадобилось бы
// и каков предел. На этапе 16 станет кодом 507.
//
// От std::bad_alloc это отличается принципиально: bad_alloc — отказ
// операционной системы, после которого состояние процесса под вопросом;
// OutOfMemoryError — штатное решение хранилища, принятое до того, как
// что-либо изменено. Поэтому на границе у них разные ветки.
class OutOfMemoryError : public KvError {
public:
    OutOfMemoryError(std::string key, std::size_t required, std::size_t limit);

    [[nodiscard]] const std::string& key() const noexcept { return key_; }
    [[nodiscard]] std::size_t required() const noexcept { return required_; }
    [[nodiscard]] std::size_t limit() const noexcept { return limit_; }

private:
    std::string key_;
    std::size_t required_;
    std::size_t limit_;
};

// Целочисленное переполнение в INCR. Этого класса нет в постановке: его нашёл
// фаззер из домашнего задания. До него INCR с шагом около INT64_MAX давал
// переполнение знакового целого — неопределённое поведение, которое
// санитайзер неопределённого поведения ловит, а обычная сборка молча
// превращает в отрицательное число. На этапе 16 станет кодом 409.
class OverflowError : public KvError {
public:
    OverflowError(std::string key, std::int64_t current, std::int64_t by);

    [[nodiscard]] const std::string& key() const noexcept { return key_; }

private:
    std::string key_;
};

// Звёздочка этапа 3: разворот цепочки, собранной std::throw_with_nested.
//
// Это часть границы, а не ядра: обе функции перехватывают исключения, чтобы
// заглянуть внутрь, и вызываются только из handle и из тестов.

// Самое внутреннее исключение цепочки — исходная причина.
[[nodiscard]] std::exception_ptr root_cause(std::exception_ptr error);

// Тексты внешних звеньев, от внешнего к внутреннему, без самой причины:
// «while executing MSET a b c» и так далее.
[[nodiscard]] std::vector<std::string> context_of(std::exception_ptr error);

}  // namespace kvs
