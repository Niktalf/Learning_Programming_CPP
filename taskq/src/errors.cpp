#include "taskq/errors.hpp"

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace taskq {

// Сообщение для what() собирается один раз, в конструкторе. Сложением строк
// и std::to_string, а не std::format: тот появится с C++23 на этапе 5.

ParseError::ParseError(std::string reason, std::string input)
    : TaskQError("parse error: " + reason + " in \"" + input + "\""),
      reason_(std::move(reason)),
      input_(std::move(input)) {}

UnknownQueueError::UnknownQueueError(std::string queue)
    : TaskQError("no such queue: " + queue), queue_(std::move(queue)) {}

UnknownTaskError::UnknownTaskError(TaskId id)
    : TaskQError("no such task: " + std::to_string(id)), id_(id) {}

UnknownWorkerError::UnknownWorkerError(std::string worker)
    : TaskQError("no such worker: " + worker), worker_(std::move(worker)) {}

InvalidTransitionError::InvalidTransitionError(TaskId id, State from, State to)
    : TaskQError("task " + std::to_string(id) + ": " + std::string(name(from)) + " -> " +
                 std::string(name(to)) + " not allowed"),
      id_(id),
      from_(from),
      to_(to) {}

WrongStateError::WrongStateError(TaskId id, State actual, std::string operation)
    : TaskQError(operation + " is not possible for task " + std::to_string(id) + ", it is " +
                 std::string(name(actual))),
      id_(id),
      actual_(actual),
      operation_(std::move(operation)) {}

TypeMismatchError::TypeMismatchError(TaskId id, std::string expected, std::string actual)
    : TaskQError("task " + std::to_string(id) + " payload is " + actual + ", expected " +
                 expected),
      id_(id),
      expected_(std::move(expected)),
      actual_(std::move(actual)) {}

OverflowError::OverflowError(TaskId id, std::int64_t current, std::int64_t by)
    : TaskQError("BUMP would overflow task " + std::to_string(id) + ": " +
                 std::to_string(current) + " + " + std::to_string(by)),
      id_(id) {}

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

}  // namespace taskq
