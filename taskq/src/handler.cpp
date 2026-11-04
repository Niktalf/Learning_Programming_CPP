#include "taskq/handler.hpp"

#include <exception>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/parser.hpp"

namespace taskq {
namespace {

// Ответ по исходной причине ошибки. Порядок веток важен: от частного к
// общему. Если поставить TaskQError первым, до ParseError очередь не дойдёт
// никогда.
//
// Все ветки ловят по константной ссылке. По значению нельзя: объект
// обрезался бы до базового класса, и поля потомка потерялись бы.
[[nodiscard]] std::string reason_of(const std::exception_ptr& cause) {
    try {
        std::rethrow_exception(cause);
    } catch (const ParseError& e) {
        return e.reason();
    } catch (const UnknownQueueError& e) {
        return "no such queue: " + e.queue() + " (use CREATE)";
    } catch (const UnknownTaskError& e) {
        return "no such task: " + std::to_string(e.id());
    } catch (const UnknownWorkerError& e) {
        return "no such worker: " + e.worker() + " (use WORKER)";
    } catch (const InvalidTransitionError& e) {
        return "task " + std::to_string(e.id()) + " is " + std::string(name(e.from())) +
               ", cannot become " + std::string(name(e.to()));
    } catch (const WrongStateError& e) {
        return e.operation() + " needs another state, task " + std::to_string(e.id()) +
               " is " + std::string(name(e.actual()));
    } catch (const TypeMismatchError& e) {
        return "wrong type: expected " + e.expected() + ", got " + e.actual();
    } catch (const OverflowError&) {
        return "BUMP would overflow";
    } catch (const TaskQError& e) {
        return e.what();
    } catch (const std::bad_alloc&) {
        return "out of memory (allocation failed)";
    } catch (const std::exception& e) {
        return std::string("internal: ") + e.what();
    } catch (...) {
        return "internal: unknown error";
    }
}

}  // namespace

Response handle(Service& service, std::string line) {
    const InputLine input{std::move(line)};
    try {
        return service.execute(parse(input));
    } catch (...) {
        // catch (...) допустим только здесь, на самой внешней границе, и
        // только с осмысленным ответом. Проглотить молча было бы хуже, чем
        // упасть.
        //
        // Ответ строится по исходной причине, а цепочка контекста, если она
        // есть, приписывается в скобках: «ERR no such task: 7 (while
        // executing ACK 7)». Клиенту видно и что случилось, и на какой
        // команде.
        const std::exception_ptr error = std::current_exception();
        std::string text = "ERR " + reason_of(root_cause(error));
        for (const std::string& step : context_of(error)) {
            text += " (" + step + ")";
        }
        return Response{false, std::move(text)};
    }
}

}  // namespace taskq
