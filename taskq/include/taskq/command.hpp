#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

#include "taskq/task.hpp"

namespace taskq {

// Каждая команда — отдельный тип, а не набор полей с признаком. Благодаря
// этому std::visit проверяет, что обработан весь набор: добавили команду —
// код не соберётся, пока не появится обработчик.
//
// Поля хранят std::string_view: команда живёт не дольше строки, из которой
// разобрана. За это отвечает InputLine, см. parser.hpp.
struct SubmitCommand { std::string_view queue; int priority; std::string_view body; };
struct PollCommand   { std::string_view queue; };
struct AckCommand    { TaskId id; };
struct FailCommand   { TaskId id; };
struct StatusCommand { TaskId id; };
struct TypeCommand   { TaskId id; };
struct BumpCommand   { TaskId id; std::int64_t by; };

using Command = std::variant<SubmitCommand, PollCommand, AckCommand, FailCommand,
                             StatusCommand, TypeCommand, BumpCommand>;

}  // namespace taskq
