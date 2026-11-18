#include "kvs/errors.hpp"

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace kvs {

// Сообщение для what() собирается один раз, в конструкторе. Сложением строк
// и std::to_string, а не std::format: тот появится с C++23 на этапе 5.

ParseError::ParseError(std::string reason, std::string input)
    : KvError("parse error: " + reason + " in \"" + input + "\""),
      reason_(std::move(reason)),
      input_(std::move(input)) {}

KeyNotFoundError::KeyNotFoundError(std::string key)
    : KvError("no such key: " + key), key_(std::move(key)) {}

TypeMismatchError::TypeMismatchError(std::string key, std::string expected,
                                     std::string actual)
    : KvError("type mismatch for key " + key + ": expected " + expected + ", got " +
              actual),
      key_(std::move(key)),
      expected_(std::move(expected)),
      actual_(std::move(actual)) {}

OutOfMemoryError::OutOfMemoryError(std::string key, std::size_t required, std::size_t limit)
    : KvError("out of memory writing key " + key + ": " + std::to_string(required) +
              " bytes required, limit is " + std::to_string(limit)),
      key_(std::move(key)),
      required_(required),
      limit_(limit) {}

OverflowError::OverflowError(std::string key, std::int64_t current, std::int64_t by)
    : KvError("increment would overflow key " + key + ": " + std::to_string(current) +
              " + " + std::to_string(by)),
      key_(std::move(key)) {}

namespace {

// Следующее звено цепочки или пустой указатель, если звено последнее.
// Внешнее исключение, созданное std::throw_with_nested, наследует и тип
// контекста, и std::nested_exception; поймав его как nested_exception,
// получаем доступ к вложенному.
[[nodiscard]] std::exception_ptr inner_of(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::nested_exception& nested) {
        return nested.nested_ptr();
    } catch (...) {
        return nullptr;
    }
}

[[nodiscard]] std::string message_of(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown error";
    }
}

}  // namespace

std::exception_ptr root_cause(std::exception_ptr error) {
    for (std::exception_ptr inner = inner_of(error); inner; inner = inner_of(error)) {
        error = inner;
    }
    return error;
}

std::vector<std::string> context_of(std::exception_ptr error) {
    std::vector<std::string> context;
    for (std::exception_ptr inner = inner_of(error); inner; inner = inner_of(error)) {
        context.push_back(message_of(error));
        error = inner;
    }
    return context;
}

}  // namespace kvs
