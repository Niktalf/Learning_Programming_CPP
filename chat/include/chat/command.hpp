#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace chat {

// Каждая команда — отдельный тип, а не набор полей с признаком. Благодаря
// этому std::visit проверяет, что обработан весь набор: добавили команду —
// код не соберётся, пока не появится обработчик.
//
// Поля хранят std::string_view: команда живёт не дольше строки, из которой
// разобрана. За это отвечает InputLine, см. parser.hpp.
struct JoinCommand    { std::string_view room; };
struct LeaveCommand   { std::string_view room; };
struct WhoCommand     { std::string_view room; };
struct HistoryCommand { std::string_view room; std::size_t limit; };
struct TypeCommand    { std::string_view room; std::size_t index; };
struct ReactCommand   { std::string_view room; std::size_t index; std::int64_t by; };

// Этап 3: сообщение можно отправить сразу в несколько комнат — /msg #a,#b
// текст, как в IRC. Рассылка атомарна: либо сообщение появится во всех
// комнатах, либо ни в одной (см. BasicChatStore::post_many).
using Rooms = std::vector<std::string_view>;
struct MsgCommand     { Rooms rooms; std::string_view text; };

// Этап 3: смена ника. Ради неё появился реестр ников и NicknameTakenError.
struct NickCommand    { std::string_view nick; };

// Пустая тема означает «показать текущую», заданная — «установить».
struct TopicCommand   { std::string_view room; std::optional<std::string_view> topic; };

using Command = std::variant<JoinCommand, LeaveCommand, MsgCommand, WhoCommand,
                             HistoryCommand, TypeCommand, ReactCommand, TopicCommand,
                             NickCommand>;

}  // namespace chat
