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
    publish_events();   // уход из комнат — UserLeft
}

Response Server::handle(Session& session, std::string line) {
    const InputLine input{std::move(line)};
    store_.touch(session.nick);
    try {
        // Журнал пишет сам обработчик /msg: только он знает, что
        // отправлено. Сервер лишь даёт ему журнал, если тот ведётся.
        Context call{store_, session, log_.get()};
        Response response = commands().bind(input.view())(call);
        publish_events();
        return response;
    } catch (...) {
        // Команды хранилища дают сильную гарантию, и неудачная команда
        // событий не оставляет. Но если журнал бросил после отправки,
        // сообщение в комнате уже есть — и событие о нём уходит.
        publish_events();
        // catch (...) допустим только здесь, на самой внешней границе, и
        // только с осмысленным ответом. Формат ответа — дело Reply (этап 9).
        return Reply::error(std::current_exception());
    }
}

void Server::publish_events() noexcept {
    if (!events_) {
        return;
    }
    for (const DomainEvent& event : store_.take_events()) {
        try {
            events_->publish(event);
        } catch (...) {
            // Решение этапа 10 (docs/events.md, домашнее задание): событие
            // теряется, команда остаётся выполненной. Сообщение уже в
            // комнате, и отказывать отправителю из-за брокера сервер не
            // будет.
            ++lost_events_;
        }
    }
}

}  // namespace chat
