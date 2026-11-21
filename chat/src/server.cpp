#include "chat/server.hpp"

#include <exception>
#include <string>
#include <utility>

#include "chat/errors.hpp"
#include "chat/commands.hpp"
#include "chat/input_line.hpp"
#include "chat/reply.hpp"

namespace chat {

Session Server::login(std::string nick) {
    store_.register_user(nick);   // занятый ник — NicknameTakenError
    return Session{std::move(nick)};
}

void Server::logout(const Session& session) {
    store_.release_user(session.nick);
}

Response Server::handle(Session& session, std::string line) {
    const InputLine input{std::move(line)};
    store_.touch(session.nick);
    try {
        // Журнал пишет сам обработчик /msg: только он знает, что
        // отправлено. Сервер лишь даёт ему журнал, если тот ведётся.
        Context call{store_, session, log_.get()};
        return commands().bind(input.view())(call);
    } catch (...) {
        // catch (...) допустим только здесь, на самой внешней границе, и
        // только с осмысленным ответом. Формат ответа — дело Reply (этап 9).
        return Reply::error(std::current_exception());
    }
}

}  // namespace chat
