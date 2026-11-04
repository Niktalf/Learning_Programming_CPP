#include "kvs/handler.hpp"

#include <exception>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "kvs/errors.hpp"
#include "kvs/parser.hpp"

namespace kvs {
namespace {

// Ответ по исходной причине ошибки. Порядок веток важен: от частного к
// общему. Если поставить KvError первым, до ParseError очередь не дойдёт
// никогда.
//
// Все ветки ловят по константной ссылке. По значению нельзя: объект
// обрезался бы до базового класса, и поля потомка потерялись бы.
[[nodiscard]] std::string reason_of(const std::exception_ptr& cause) {
    try {
        std::rethrow_exception(cause);
    } catch (const ParseError& e) {
        return e.reason();
    } catch (const KeyNotFoundError& e) {
        return "no such key: " + e.key();
    } catch (const TypeMismatchError& e) {
        return "wrong type: expected " + e.expected() + ", got " + e.actual();
    } catch (const OutOfMemoryError& e) {
        return "out of memory: " + std::to_string(e.required()) + " bytes required, limit is " +
               std::to_string(e.limit());
    } catch (const OverflowError&) {
        return "increment would overflow";
    } catch (const KvError& e) {
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

Response handle(Store& store, std::string line) {
    const InputLine input{std::move(line)};
    try {
        return execute(store, parse(input));
    } catch (...) {
        // catch (...) допустим только здесь, на самой внешней границе, и
        // только с осмысленным ответом. Проглотить молча было бы хуже, чем
        // упасть.
        //
        // Ответ строится по исходной причине, а цепочка контекста, если она
        // есть, приписывается в скобках: «ERR no such key: k (while executing
        // GET k)». Клиенту видно и что случилось, и на какой команде.
        const std::exception_ptr error = std::current_exception();
        std::string text = "ERR " + reason_of(root_cause(error));
        const std::vector<std::string> context = context_of(error);
        for (const std::string& step : context) {
            text += " (" + step + ")";
        }
        return Response{false, std::move(text)};
    }
}

}  // namespace kvs
