#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "taskq/task.hpp"

namespace taskq {

// Каждая команда — отдельный тип, а не набор полей с признаком. Благодаря
// этому std::visit проверяет, что обработан весь набор: добавили команду —
// код не соберётся, пока не появится обработчик.
//
// Поля хранят std::string_view: команда живёт не дольше строки, из которой
// разобрана. За это отвечает InputLine, см. parser.hpp.
struct CreateCommand  { std::string_view queue; };
struct SubmitCommand  { std::string_view queue; int priority; std::string_view body; };
// Этап 4: POLL от имени воркера — POLL build w1. Без воркера аренда
// анонимная: брошенной она не станет, вернуть её можно только RELEASE.
struct PollCommand    { std::string_view queue; std::optional<std::string_view> worker; };
struct AckCommand     { TaskId id; };
struct FailCommand    { TaskId id; };
struct StatusCommand  { TaskId id; };
struct TypeCommand    { TaskId id; };
struct BumpCommand    { TaskId id; std::int64_t by; };

// Этап 3: возврат аренды — переход Leased -> Pending. Пока его вызывает
// клиент; на этапе 11 то же самое будет делать сборщик просроченных аренд.
struct ReleaseCommand { TaskId id; };

// Этап 3: пакетная постановка. Либо поставлены все задачи пакета, либо ни
// одной — см. BasicTaskStore::submit_batch.
struct BatchCommand   { std::vector<SubmitCommand> items; };

// Этап 4: реестр воркеров. WORKER регистрирует, GONE — исчезновение
// воркера (на этапе 15 так будет выглядеть разорванное соединение), REAP
// возвращает в очереди задачи, чей держатель исчез.
struct WorkerCommand  { std::string_view name; };
struct GoneCommand    { std::string_view name; };
struct ReapCommand    {};

// Этап 5: LIST build prio>=5 type=number limit=10 — задачи очереди через
// ленивый конвейер ranges. Все аргументы именованные и необязательные.
struct ListCommand    { std::string_view queue; int min_priority; std::optional<std::string_view> kind; std::size_t limit; };

inline constexpr std::size_t default_list_limit = 20;

using Command = std::variant<CreateCommand, SubmitCommand, BatchCommand, PollCommand,
                             AckCommand, FailCommand, ReleaseCommand, StatusCommand,
                             TypeCommand, BumpCommand, WorkerCommand, GoneCommand,
                             ReapCommand, ListCommand>;

}  // namespace taskq
