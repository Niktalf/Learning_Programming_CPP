#include "chat/executor.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

#include "chat/overloaded.hpp"

namespace chat {
namespace {

// Сообщение вида :thumbsup: — реакция, а не текст. Отдельной команды для
// реакций в постановке нет; так её и отправляют в большинстве чатов.
[[nodiscard]] bool is_reaction(std::string_view text) noexcept {
    if (text.size() < 3 || text.front() != ':' || text.back() != ':') {
        return false;
    }
    for (const char c : text.substr(1, text.size() - 2)) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Body make_body(std::string_view text) {
    if (is_reaction(text)) {
        return Reaction{std::string(text.substr(1, text.size() - 2)), 1};
    }
    return TextBody{std::string(text)};
}

[[nodiscard]] Response not_a_member(std::string_view room) {
    return Response{false, "ERR you are not in " + std::string(room) + " (use /join)"};
}

[[nodiscard]] Response no_room(std::string_view room) {
    return Response{false, "ERR no such room: " + std::string(room)};
}

[[nodiscard]] Response no_message(std::string_view room, std::size_t index) {
    std::ostringstream out;
    out << "ERR no message " << index << " in " << room;
    return Response{false, out.str()};
}

}  // namespace

Response execute(ChatStore& store, const Session& session, const Command& command) {
    return std::visit(
        overloaded{
            [&](const JoinCommand& c) {
                if (store.join(c.room, session.nick)) {
                    store.post(std::string(c.room),
                               Message(session.nick, Body{SystemNotice{session.nick + " joined"}}));
                }
                return Response{true, "OK"};
            },
            [&](const LeaveCommand& c) {
                if (!store.leave(c.room, session.nick)) {
                    return not_a_member(c.room);
                }
                store.post(std::string(c.room),
                           Message(session.nick, Body{SystemNotice{session.nick + " left"}}));
                return Response{true, "OK"};
            },
            [&](const MsgCommand& c) {
                if (!store.is_member(c.room, session.nick)) {
                    return not_a_member(c.room);
                }
                store.post(std::string(c.room), Message(session.nick, make_body(c.text)));
                return Response{true, "OK"};
            },
            [&](const WhoCommand& c) {
                // if с инициализацией: результат ограничен областью проверки.
                if (const auto members = store.members(c.room); members) {
                    std::ostringstream out;
                    for (const std::string& nick : members->get()) {
                        out << nick << '\n';
                    }
                    const std::string text = out.str();
                    return Response{true, text.empty() ? "(empty)" : text.substr(0, text.size() - 1)};
                }
                return no_room(c.room);
            },
            [&](const HistoryCommand& c) {
                if (const auto history = store.history(c.room); history) {
                    const History& messages = history->get();
                    const std::size_t from =
                        messages.size() > c.limit ? messages.size() - c.limit : 0;
                    std::ostringstream out;
                    for (std::size_t i = from; i < messages.size(); ++i) {
                        out << i << ": <" << messages[i].author() << "> "
                            << messages[i].to_string() << '\n';
                    }
                    const std::string text = out.str();
                    return Response{true, text.empty() ? "(empty)" : text.substr(0, text.size() - 1)};
                }
                return no_room(c.room);
            },
            [&](const TypeCommand& c) {
                if (const auto message = store.message_at(c.room, c.index); message) {
                    return Response{true, std::string(message->get().type_name())};
                }
                return no_message(c.room, c.index);
            },
            [&](const ReactCommand& c) {
                const auto message = store.message_at(c.room, c.index);
                if (!message) {
                    return no_message(c.room, c.index);
                }
                // get_if вместо std::get: реакция на текстовое сообщение —
                // штатная ошибка пользователя, а не исключительная ситуация.
                // Исключения появятся на этапе 3.
                Reaction* const reaction = message->get().get_if<Reaction>();
                if (reaction == nullptr) {
                    return Response{false, "ERR REACT needs a reaction, the message is " +
                                               std::string(message->get().type_name())};
                }
                reaction->count += c.by;
                std::ostringstream out;
                out << reaction->count;
                return Response{true, out.str()};
            },
            [&](const TopicCommand& c) {
                if (!c.topic) {
                    if (const auto topic = store.topic(c.room); topic) {
                        return Response{true, topic->empty() ? "(no topic)" : *topic};
                    }
                    return no_room(c.room);
                }
                if (!store.is_member(c.room, session.nick)) {
                    return not_a_member(c.room);
                }
                store.set_topic(c.room, std::string(*c.topic));
                store.post(std::string(c.room),
                           Message(session.nick, Body{SystemNotice{session.nick + " set topic: " +
                                                                   std::string(*c.topic)}}));
                return Response{true, "OK"};
            },
        },
        command);
}

}  // namespace chat
