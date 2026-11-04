#include "chat/errors.hpp"

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace chat {

// Сообщение для what() собирается один раз, в конструкторе. Сложением строк
// и std::to_string, а не std::format: тот появится с C++23 на этапе 5.

ParseError::ParseError(std::string reason, std::string input)
    : ChatError("parse error: " + reason + " in \"" + input + "\""),
      reason_(std::move(reason)),
      input_(std::move(input)) {}

UnknownRoomError::UnknownRoomError(std::string room)
    : ChatError("no such room: " + room), room_(std::move(room)) {}

NotMemberError::NotMemberError(std::string room, std::string nick)
    : ChatError(nick + " is not in " + room), room_(std::move(room)), nick_(std::move(nick)) {}

NicknameTakenError::NicknameTakenError(std::string nick)
    : ChatError("nickname already in use: " + nick), nick_(std::move(nick)) {}

MessageNotFoundError::MessageNotFoundError(std::string room, std::size_t index)
    : ChatError("no message " + std::to_string(index) + " in " + room),
      room_(std::move(room)),
      index_(index) {}

TypeMismatchError::TypeMismatchError(std::string room, std::size_t index, std::string expected,
                                     std::string actual)
    : ChatError("message " + std::to_string(index) + " in " + room + " is " + actual +
                ", expected " + expected),
      room_(std::move(room)),
      index_(index),
      expected_(std::move(expected)),
      actual_(std::move(actual)) {}

CounterOverflowError::CounterOverflowError(std::string room, std::size_t index,
                                           std::int64_t current, std::int64_t by)
    : ChatError("reaction counter would overflow: message " + std::to_string(index) + " in " +
                room + ", " + std::to_string(current) + " + " + std::to_string(by)),
      room_(std::move(room)),
      index_(index) {}

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

}  // namespace chat
