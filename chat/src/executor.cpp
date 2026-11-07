#include "chat/executor.hpp"

#include <cstdint>
#include <exception>
#include <format>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "chat/errors.hpp"
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

[[nodiscard]] MessagePtr make_message(const std::string& author, std::string_view text) {
    if (is_reaction(text)) {
        return make_reaction(author, std::string(text.substr(1, text.size() - 2)));
    }
    return make_text(author, std::string(text));
}

// Комната существует, и сессия в ней состоит. Одна точка, где отказ
// превращается в исключение, — чтобы не повторять проверку в каждой команде.
void require_member(const ChatStore& store, std::string_view room, const Session& session) {
    if (!store.contains(room)) {
        throw UnknownRoomError(std::string(room));
    }
    if (!store.is_member(room, session.nick)) {
        throw NotMemberError(std::string(room), session.nick);
    }
}

// Сложение с проверкой переполнения. Необходимость нашёл фаззер из домашнего
// задания: переполнение знакового целого — неопределённое поведение.
[[nodiscard]] std::int64_t checked_add(std::string_view room, std::size_t index,
                                       std::int64_t current, std::int64_t by) {
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();
    if ((by > 0 && current > max - by) || (by < 0 && current < min - by)) {
        throw CounterOverflowError(std::string(room), index, current, by);
    }
    return current + by;
}

// Сообщение с идентификатором от хранилища. Все отправки идут через эту
// функцию: сообщение без идентификатора в истории не появляется.
[[nodiscard]] MessagePtr stamped(ChatStore& store, MessagePtr message) {
    message->assign_id(store.next_message_id(message->author()));
    return message;
}

// Склейка строк через перевод строки — views::join_with из C++23, без
// ручного счёта, где ставить разделитель.
[[nodiscard]] std::string joined(const std::vector<std::string>& lines, const char* empty) {
    if (lines.empty()) {
        return empty;
    }
    return std::ranges::to<std::string>(lines | std::views::join_with('\n'));
}

// Короткое описание команды для цепочки контекста: глагол и комнаты.
[[nodiscard]] std::string describe(const Command& command) {
    return std::visit(
        overloaded{
            [](const JoinCommand& c) { return "/join " + std::string(c.room); },
            [](const LeaveCommand& c) { return "/leave " + std::string(c.room); },
            [](const MsgCommand& c) {
                std::string text = "/msg";
                char separator = ' ';
                for (const std::string_view room : c.rooms) {
                    text += separator;
                    text += room;
                    separator = ',';
                }
                return text;
            },
            [](const WhoCommand& c) { return "/who " + std::string(c.room); },
            [](const HistoryCommand& c) { return "/history " + std::string(c.room); },
            [](const TypeCommand& c) { return "/type " + std::string(c.room); },
            [](const ReactCommand& c) { return "/react " + std::string(c.room); },
            [](const TopicCommand& c) { return "/topic " + std::string(c.room); },
            [](const NickCommand& c) { return "/nick " + std::string(c.nick); },
        },
        command);
}

Response run_command(ChatStore& store, Session& session, const Command& command) {
    return std::visit(
        overloaded{
            [&](const JoinCommand& c) {
                if (store.join(c.room, session.nick)) {
                    store.post(c.room, stamped(store, make_notice(session.nick, session.nick + " joined")));
                }
                return Response{true, "OK"};
            },
            [&](const LeaveCommand& c) {
                require_member(store, c.room, session);
                store.leave(c.room, session.nick);
                store.post(c.room, stamped(store, make_notice(session.nick, session.nick + " left")));
                return Response{true, "OK"};
            },
            [&](const MsgCommand& c) {
                // Все проверки — до рассылки: отказ в третьей комнате не должен
                // оставить сообщение в первых двух. Сама рассылка атомарна.
                for (const std::string_view room : c.rooms) {
                    require_member(store, room, session);
                }
                store.post_many(c.rooms, stamped(store, make_message(session.nick, c.text)));
                return Response{true, "OK"};
            },
            [&](const WhoCommand& c) {
                const auto members = store.members(c.room);
                if (!members) {
                    throw UnknownRoomError(std::string(c.room));
                }
                // transform превращает участников в ники прямо в конвейере.
                const auto nicks = *members | std::views::transform([](const auto& user) {
                    return user->nick();
                });
                return Response{true, joined(std::ranges::to<std::vector<std::string>>(nicks),
                                             "(empty)")};
            },
            [&](const HistoryCommand& c) {
                // Этап 5: последние limit сообщений, при необходимости — только
                // одного автора, ленивым конвейером ranges (history_where).
                const auto selected = store.history_where(
                    c.room,
                    [&c](const MessagePtr& message) {
                        return !c.author || message->author() == *c.author;
                    },
                    c.limit);
                if (!selected) {
                    throw UnknownRoomError(std::string(c.room));
                }
                std::vector<std::string> lines;
                for (const auto& [index, message] : *selected) {
                    lines.push_back(std::format("{}: <{}> {}", index, message.get()->author(),
                                                message.get()->to_string()));
                }
                return Response{true, joined(lines, "(empty)")};
            },
            [&](const TypeCommand& c) {
                if (!store.contains(c.room)) {
                    throw UnknownRoomError(std::string(c.room));
                }
                const auto message = std::as_const(store).message_at(c.room, c.index);
                if (!message) {
                    throw MessageNotFoundError(std::string(c.room), c.index);
                }
                return Response{true, std::string(message->get()->type_name())};
            },
            [&](const ReactCommand& c) {
                if (!store.contains(c.room)) {
                    throw UnknownRoomError(std::string(c.room));
                }
                const auto message = store.message_at(c.room, c.index);
                if (!message) {
                    throw MessageNotFoundError(std::string(c.room), c.index);
                }
                // Этап 4: вид сообщения определяет класс, проверка —
                // dynamic_cast: указатель или nullptr. Неверный вид — ошибка
                // с контекстом в полях.
                IMessage& target = *message->get();
                auto* const reaction = dynamic_cast<ReactionMessage*>(&target);
                if (reaction == nullptr) {
                    throw TypeMismatchError(std::string(c.room), c.index, "reaction",
                                            std::string(target.type_name()));
                }
                // Сначала вычисляем, потом присваиваем: бросок из checked_add
                // оставляет счётчик прежним.
                reaction->set_count(checked_add(c.room, c.index, reaction->count(), c.by));
                return Response{true, std::to_string(reaction->count())};
            },
            [&](const TopicCommand& c) {
                if (!c.topic) {
                    const auto topic = store.topic(c.room);
                    if (!topic) {
                        throw UnknownRoomError(std::string(c.room));
                    }
                    return Response{true, topic->empty() ? "(no topic)" : *topic};
                }
                require_member(store, c.room, session);
                const std::string topic(*c.topic);
                store.set_topic(c.room, topic);
                store.post(c.room, stamped(store, make_notice(session.nick, session.nick + " set topic: " + topic)));
                return Response{true, "OK"};
            },
            [&](const NickCommand& c) {
                std::string next(c.nick);
                store.rename(session.nick, next);
                // Сессия меняется последней: если rename бросил, ник прежний и
                // в реестре, и в сессии.
                session.nick = std::move(next);
                return Response{true, "OK, you are " + session.nick};
            },
        },
        command);
}

}  // namespace

// Звёздочка этапа 3: цепочка контекста. Ошибка из глубины — из хранилища или
// из проверки вида сообщения — не знает, какая команда её вызвала. Исполнитель
// перехватывает её только затем, чтобы приложить это знание, и тут же бросает
// дальше: std::throw_with_nested создаёт новое исключение, внутри которого
// лежит исходное. Ничего не решается и не проглатывается — разворачивает
// цепочку и решает, что ответить, по-прежнему только граница handle.
Response execute(ChatStore& store, Session& session, const Command& command) {
    try {
        return run_command(store, session, command);
    } catch (...) {
        std::throw_with_nested(ChatError("while executing " + describe(command)));
    }
}

}  // namespace chat
