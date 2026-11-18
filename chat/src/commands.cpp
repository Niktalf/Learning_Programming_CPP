#include "chat/commands.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "chat/errors.hpp"
#include "chat/message.hpp"

namespace chat {
namespace {

[[nodiscard]] Response ok(std::string text) {
    return Response{true, std::move(text)};
}

// Сообщение вида :thumbsup: — реакция, а не текст. Отдельной команды для
// реакций в постановке нет; так её и отправляют в большинстве чатов.
[[nodiscard]] bool is_reaction(std::string_view text) noexcept {
    if (text.size() < 3 || text.front() != ':' || text.back() != ':') {
        return false;
    }
    // Этап 8: all_of вместо цикла с ранним выходом.
    return std::ranges::all_of(text.substr(1, text.size() - 2), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
               c == '+' || c == '-';
    });
}

[[nodiscard]] MessagePtr make_message(const std::string& author, std::string_view text) {
    if (is_reaction(text)) {
        return make_reaction(author, std::string(text.substr(1, text.size() - 2)));
    }
    return make_text(author, std::string(text));
}

// Комната существует, и участник в ней состоит. Одна точка, где отказ
// превращается в исключение, — чтобы не повторять проверку в каждой команде.
void require_member(const ChatStore& store, std::string_view room, const std::string& nick) {
    if (!store.contains(room)) {
        throw UnknownRoomError(std::string(room));
    }
    if (!store.is_member(room, nick)) {
        throw NotMemberError(std::string(room), nick);
    }
}

void require_room(const ChatStore& store, std::string_view room) {
    if (!store.contains(room)) {
        throw UnknownRoomError(std::string(room));
    }
}

// Сложение с проверкой переполнения. Необходимость нашёл фаззер этапа 3:
// переполнение знакового целого — неопределённое поведение.
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

[[nodiscard]] MessagePtr notice(ChatStore& store, const std::string& author, std::string text) {
    return stamped(store, make_notice(author, std::move(text)));
}

// Склейка строк через перевод строки — views::join_with из C++23.
[[nodiscard]] std::string joined(const std::vector<std::string>& lines, const char* empty) {
    if (lines.empty()) {
        return empty;
    }
    return std::ranges::to<std::string>(lines | std::views::join_with('\n'));
}

// Строки ответа истории: «номер: <автор> текст», с отметкой времени —
// «номер @ms: <автор> текст». Этап 8: transform вместо цикла с push_back.
[[nodiscard]] std::vector<std::string> lines_of(const std::vector<ChatStore::Entry>& entries, bool with_time) {
    std::vector<std::string> lines;
    lines.reserve(entries.size());
    std::ranges::transform(entries, std::back_inserter(lines), [with_time](const ChatStore::Entry& entry) {
        const IMessage& item = *entry.message.get();
        if (with_time) {
            return std::format("{} @{}: <{}> {}", entry.index, item.id().timestamp, item.author(), item.to_string());
        }
        return std::format("{}: <{}> {}", entry.index, item.author(), item.to_string());
    });
    return lines;
}

// Запись журнала: комнаты через запятую, автор и текст через табуляцию.
// Табуляции в тексте быть не может — convert отвергает управляющие символы.
[[nodiscard]] std::string record_of(const std::string& author, const RoomList& rooms,
                                    std::string_view text) {
    const auto names = std::ranges::to<std::string>(rooms.rooms | std::views::join_with(','));
    return std::format("{}\t{}\t{}", names, author, text);
}

// --- Обработчики. Первый параметр — контекст, остальные — аргументы
// команды; реестр разбирает их по типам параметров сам. -------------------

Response join(Context& call, RoomName room) {
    const std::string& nick = call.session.nick;
    if (call.store.join(room.name, nick)) {
        call.store.post(room.name, notice(call.store, nick, nick + " joined"));
    }
    return ok("OK");
}

Response leave(Context& call, RoomName room) {
    const std::string& nick = call.session.nick;
    require_member(call.store, room.name, nick);
    call.store.leave(room.name, nick);
    call.store.post(room.name, notice(call.store, nick, nick + " left"));
    return ok("OK");
}

// /msg #a,#b текст: RoomList — один токен со списком, Tail — весь остаток.
Response message(Context& call, RoomList rooms, Tail text) {
    // Все проверки — до рассылки: отказ в третьей комнате не должен оставить
    // сообщение в первых двух. Сама рассылка атомарна.
    std::ranges::for_each(rooms.rooms,
                          [&call](std::string_view room) { require_member(call.store, room, call.session.nick); });
    call.store.post_many(rooms.rooms, stamped(call.store, make_message(call.session.nick, text.text)));
    // В журнал — только успешно отправленные сообщения. Если запись не
    // удалась, сообщение уже в истории, а клиент получит ошибку; журнал до
    // исполнения (write-ahead) этого не допускает, но требует отката. Курс
    // вернётся к этому на этапе 17.
    if (call.journal != nullptr) {
        call.journal->append(record_of(call.session.nick, rooms, text.text));
    }
    return ok("OK");
}

Response who(Context& call, RoomName room) {
    const auto members = call.store.members(room.name);
    if (!members) {
        throw UnknownRoomError(std::string(room.name));
    }
    const auto nicks = *members | std::views::transform([](const auto& user) { return user->nick(); });
    return ok(joined(std::ranges::to<std::vector<std::string>>(nicks), "(empty)"));
}

// /history #room [limit] [nick]: два необязательных аргумента подряд — и
// арность «от 1 до 3» реестр выводит сам.
Response history(Context& call, RoomName room, std::optional<std::size_t> limit,
                 std::optional<Nick> author) {
    const auto selected = call.store.history_where(
        room.name,
        [&author](const MessagePtr& item) { return !author || item->author() == author->name; },
        limit.value_or(default_history_limit));
    if (!selected) {
        throw UnknownRoomError(std::string(room.name));
    }
    return ok(joined(lines_of(*selected, false), "(empty)"));
}

// Этап 8: история по времени. /since #room <ms> [limit] — первые limit
// сообщений с отметкой не раньше ms; /until #room <ms> [limit] — последние
// limit сообщений с отметкой не позже ms. Отметка — миллисекунды от эпохи,
// она печатается в каждой строке ответа: так её можно взять для следующего
// запроса. Поиск — по индексу по времени, красно-чёрному дереву.
Response since(Context& call, RoomName room, std::uint64_t moment, std::optional<std::size_t> limit) {
    const auto selected = call.store.history_between(room.name, moment, std::numeric_limits<std::uint64_t>::max(),
                                                     limit.value_or(default_history_limit),
                                                     ChatStore::Window::Earliest);
    if (!selected) {
        throw UnknownRoomError(std::string(room.name));
    }
    return ok(joined(lines_of(*selected, true), "(empty)"));
}

Response until(Context& call, RoomName room, std::uint64_t moment, std::optional<std::size_t> limit) {
    const auto selected = call.store.history_between(room.name, 0, moment, limit.value_or(default_history_limit),
                                                     ChatStore::Window::Latest);
    if (!selected) {
        throw UnknownRoomError(std::string(room.name));
    }
    return ok(joined(lines_of(*selected, true), "(empty)"));
}

Response type_of(Context& call, RoomName room, std::size_t index) {
    require_room(call.store, room.name);
    const auto item = std::as_const(call.store).message_at(room.name, index);
    if (!item) {
        throw MessageNotFoundError(std::string(room.name), index);
    }
    return ok(std::string(item->get()->type_name()));
}

Response react(Context& call, RoomName room, std::size_t index, std::optional<std::int64_t> step) {
    require_room(call.store, room.name);
    const auto item = call.store.message_at(room.name, index);
    if (!item) {
        throw MessageNotFoundError(std::string(room.name), index);
    }
    // Этап 4: вид сообщения определяет класс, проверка — dynamic_cast.
    IMessage& target = *item->get();
    auto* const reaction = dynamic_cast<ReactionMessage*>(&target);
    if (reaction == nullptr) {
        throw TypeMismatchError(std::string(room.name), index, "reaction",
                                std::string(target.type_name()));
    }
    // Сначала вычисляем, потом присваиваем: бросок из checked_add оставляет
    // счётчик прежним.
    reaction->set_count(checked_add(room.name, index, reaction->count(), step.value_or(1)));
    return ok(std::to_string(reaction->count()));
}

// /topic #room — показать, /topic #room "тема" — установить. Тема из
// нескольких слов — в кавычках: без них токенов больше трёх, и реестр
// отвергнет строку с подсказкой. Это лучше, чем молча взять первое слово.
Response topic(Context& call, RoomName room, std::optional<Text> text) {
    if (!text) {
        const auto current = call.store.topic(room.name);
        if (!current) {
            throw UnknownRoomError(std::string(room.name));
        }
        return ok(current->empty() ? "(no topic)" : *current);
    }
    const std::string& nick = call.session.nick;
    require_member(call.store, room.name, nick);
    // Сначала уведомление, потом тема: post может бросить, а присваивание
    // перемещением — нет. В обратном порядке бросок post оставил бы тему
    // сменённой без следа в истории.
    std::string value(text->text);
    call.store.post(room.name, notice(call.store, nick, nick + " set topic: " + value));
    call.store.set_topic(room.name, std::move(value));
    return ok("OK");
}

Response nick(Context& call, Nick next) {
    std::string name(next.name);
    call.store.rename(call.session.nick, name);
    // Сессия меняется последней: если rename бросил, ник прежний и в реестре,
    // и в сессии.
    call.session.nick = std::move(name);
    return ok("OK, you are " + call.session.nick);
}

// --- Домашнее задание этапа 6: две новые команды, по строке регистрации. --

// /kick #room nick: исключить участника. Исключение и уведомление о нём —
// одна операция хранилища kick, а не leave и затем post: если бы post
// бросил, участник уже был бы исключён, а в истории об этом ничего. Сильная
// гарантия — внутри kick.
Response kick(Context& call, RoomName room, Nick target) {
    const std::string& nick = call.session.nick;
    require_member(call.store, room.name, nick);
    if (target.name == nick) {
        throw ChatError("you cannot kick yourself (use /leave)");
    }
    call.store.kick(room.name, target.name,
                    notice(call.store, nick, nick + " kicked " + std::string(target.name)));
    return ok("OK");
}

// /rooms [after] [count]: страница комнат по алфавиту с числом живых
// участников. Этап 8: с курсором. Курсор — имя последней комнаты
// предыдущей страницы; если за страницей есть ещё, последняя строка ответа
// подсказывает следующий запрос. Страница — upper_bound по упорядоченному
// индексу имён и take, число участников — count_if, строки — transform:
// ни одного ручного цикла.
Response rooms(Context& call, std::optional<RoomName> after, std::optional<std::size_t> count) {
    const std::size_t size = count.value_or(default_rooms_page);
    const auto page = call.store.room_page(after ? std::optional(after->name) : std::nullopt, size);
    std::vector<std::string> lines;
    lines.reserve(page.names.size() + 1);
    std::ranges::transform(page.names, std::back_inserter(lines), [&call](const std::string& name) {
        return std::format("{} ({})", name, call.store.live_members(name));
    });
    if (page.more) {
        lines.push_back(std::format("(more: /rooms {} {})", page.names.back(), size));
    }
    return ok(joined(lines, "(no rooms)"));
}

}  // namespace

const CommandRegistry<Context>& commands() {
    // Реестр строится один раз, при первом обращении; инициализация
    // локальной статической переменной потокобезопасна по стандарту.
    static const CommandRegistry<Context> registry = [] {
        CommandRegistry<Context> table;
        table.register_command("/join", join)
            .register_command("/leave", leave)
            .register_command("/msg", message)
            .register_command("/who", who)
            .register_command("/history", history)
            .register_command("/type", type_of)
            .register_command("/react", react)
            .register_command("/topic", topic)
            .register_command("/nick", nick)
            .register_command("/kick", kick)
            .register_command("/rooms", rooms)
            .register_command("/since", since)
            .register_command("/until", until);
        return table;
    }();
    return registry;
}

Response execute(ChatStore& store, Session& session, std::string_view line) {
    Context call{store, session, nullptr};
    return commands().bind(line)(call);
}

}  // namespace chat
