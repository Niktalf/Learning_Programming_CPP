#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace chat {

// Каждая команда — отдельный тип, а не набор полей с признаком. Благодаря
// этому std::visit проверяет, что обработан весь набор: добавили команду —
// код не соберётся, пока не появится обработчик.
//
// Поля хранят std::string_view: команда живёт не дольше строки, из которой
// разобрана. За это отвечает InputLine, см. parser.hpp.
struct JoinCommand    { std::string_view room; };
struct LeaveCommand   { std::string_view room; };
struct MsgCommand     { std::string_view room; std::string_view text; };
struct WhoCommand     { std::string_view room; };
struct HistoryCommand { std::string_view room; std::size_t limit; };
struct TypeCommand    { std::string_view room; std::size_t index; };
struct ReactCommand   { std::string_view room; std::size_t index; std::int64_t by; };

// Пустая тема означает «показать текущую», заданная — «установить».
struct TopicCommand   { std::string_view room; std::optional<std::string_view> topic; };

using Command = std::variant<JoinCommand, LeaveCommand, MsgCommand, WhoCommand,
                             HistoryCommand, TypeCommand, ReactCommand, TopicCommand>;

}  // namespace chat
