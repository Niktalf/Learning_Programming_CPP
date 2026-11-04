#include "chat/handler.hpp"

#include <exception>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "chat/errors.hpp"
#include "chat/parser.hpp"

namespace chat {
namespace {

// Ответ по исходной причине ошибки. Порядок веток важен: от частного к
// общему. Если поставить ChatError первым, до ParseError очередь не дойдёт
// никогда.
//
// Все ветки ловят по константной ссылке. По значению нельзя: объект
// обрезался бы до базового класса, и поля потомка потерялись бы.
[[nodiscard]] std::string reason_of(const std::exception_ptr& cause) {
    try {
        std::rethrow_exception(cause);
    } catch (const ParseError& e) {
        return e.reason();
    } catch (const UnknownRoomError& e) {
        return "no such room: " + e.room();
    } catch (const NotMemberError& e) {
        return "you are not in " + e.room() + " (use /join)";
    } catch (const NicknameTakenError& e) {
        return "nickname in use: " + e.nick();
    } catch (const MessageNotFoundError& e) {
        return "no message " + std::to_string(e.index()) + " in " + e.room();
    } catch (const TypeMismatchError& e) {
        return "wrong type: expected " + e.expected() + ", got " + e.actual();
    } catch (const CounterOverflowError&) {
        return "reaction counter would overflow";
    } catch (const ChatError& e) {
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

Response handle(ChatStore& store, Session& session, std::string line) {
    const InputLine input{std::move(line)};
    try {
        return execute(store, session, parse(input));
    } catch (...) {
        // catch (...) допустим только здесь, на самой внешней границе, и
        // только с осмысленным ответом. Проглотить молча было бы хуже, чем
        // упасть.
        //
        // Ответ строится по исходной причине, а цепочка контекста, если она
        // есть, приписывается в скобках: «ERR no such room: #x (while
        // executing /who #x)». Клиенту видно и что случилось, и на какой
        // команде.
        const std::exception_ptr error = std::current_exception();
        std::string text = "ERR " + reason_of(root_cause(error));
        for (const std::string& step : context_of(error)) {
            text += " (" + step + ")";
        }
        return Response{false, std::move(text)};
    }
}

}  // namespace chat
