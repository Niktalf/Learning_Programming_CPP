#include "chat/server.hpp"

#include <exception>
#include <new>
#include <string>
#include <utility>
#include <variant>
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
    } catch (const UnknownUserError& e) {
        return "no such user: " + e.nick();
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


// Запись журнала: комнаты через запятую, автор и текст через табуляцию.
// Табуляции в тексте быть не может — разбор отвергает управляющие символы.
[[nodiscard]] std::string record_of(const std::string& author, const MsgCommand& msg) {
    std::string record;
    char separator = '\0';
    for (const std::string_view room : msg.rooms) {
        if (separator != '\0') {
            record += separator;
        }
        record += room;
        separator = ',';
    }
    record += '\t';
    record += author;
    record += '\t';
    record += msg.text;
    return record;
}

}  // namespace

Session Server::login(std::string nick) {
    store_.register_user(nick);   // занятый ник — NicknameTakenError
    return Session{std::move(nick)};
}

void Server::logout(const Session& session) {
    store_.release_user(session.nick);
}

Response Server::handle(Session& session, std::string line) {
    const InputLine input{std::move(line)};
    try {
        const Command command = parse(input);
        Response response = execute(store_, session, command);
        // В журнал — только успешно отправленные сообщения. Если запись не
        // удалась, сообщение уже в истории, а клиент получит ошибку; журнал
        // до исполнения (write-ahead) этого не допускает, но требует отката.
        // Курс вернётся к этому на этапе 17.
        if (const auto* const msg = std::get_if<MsgCommand>(&command); msg && log_) {
            log_->append(record_of(session.nick, *msg));
        }
        return response;
    } catch (...) {
        // catch (...) допустим только здесь, на самой внешней границе, и
        // только с осмысленным ответом. Ответ строится по исходной причине,
        // цепочка контекста приписывается в скобках.
        const std::exception_ptr error = std::current_exception();
        std::string text = "ERR " + reason_of(root_cause(error));
        for (const std::string& step : context_of(error)) {
            text += " (" + step + ")";
        }
        return Response{false, std::move(text)};
    }
}

}  // namespace chat
