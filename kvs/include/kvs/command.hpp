#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

namespace kvs {

// Каждая команда — отдельный тип, а не набор полей с признаком. Благодаря
// этому std::visit проверяет, что обработан весь набор: добавили команду —
// код не соберётся, пока не появится обработчик.
//
// Поля хранят std::string_view: команда живёт не дольше строки, из которой
// разобрана. За это отвечает InputLine, см. parser.hpp.
struct SetCommand    { std::string_view key; std::string_view value; };
struct GetCommand    { std::string_view key; };
struct DelCommand    { std::string_view key; };
struct ExistsCommand { std::string_view key; };
struct TypeCommand   { std::string_view key; };
struct IncrCommand   { std::string_view key; std::int64_t by; };

using Command = std::variant<SetCommand, GetCommand, DelCommand,
                             ExistsCommand, TypeCommand, IncrCommand>;

}  // namespace kvs
