#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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

// Этап 3: установка нескольких ключей одной операцией. Требование — либо
// записаны все пары, либо ни одной.
struct MsetCommand   { std::vector<std::pair<std::string_view, std::string_view>> pairs; };

// Этап 4: операции над списком — чтобы у ListValue было применение.
struct RpushCommand  { std::string_view key; std::string_view item; };
struct LlenCommand   { std::string_view key; };

// Этап 5: KEYS стал командой с образцом и пределом — KEYS user:* 10 — и
// реализован ленивым конвейером ranges. VERSION показывает версию записи.
struct KeysCommand    { std::string_view pattern; std::size_t limit; };
struct VersionCommand { std::string_view key; };

using Command = std::variant<SetCommand, GetCommand, DelCommand, ExistsCommand,
                             TypeCommand, IncrCommand, MsetCommand, RpushCommand,
                             LlenCommand, KeysCommand, VersionCommand>;

// Предел KEYS по умолчанию: таблица может быть огромной, и выдавать её
// целиком в ответ на опечатку в образце — плохая идея.
inline constexpr std::size_t default_keys_limit = 100;

}  // namespace kvs
