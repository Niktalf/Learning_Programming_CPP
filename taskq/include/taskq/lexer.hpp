#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/result.hpp"

namespace taskq {

using Tokens = std::vector<std::string_view>;

// Особенность сервиса задач: у команды SUBMIT последний аргумент — весь остаток строки,
// а не один токен. Параметр max_tokens задаёт, после скольких обычных токенов
// брать остаток целиком.
//
// Токены — представления на исходную строку, ни одного std::string внутри.
//
// Этап 3: основная функция — try_tokenize, возвращающая ошибку значением
// (единственная ошибка лексера — незакрытая кавычка). tokenize — это
// try_tokenize(...).value(): при ошибке бросается сам ParseError. Так оба пути
// обработки ошибок, исключения и Result, построены на одном коде.
[[nodiscard]] Result<Tokens, ParseError> try_tokenize(
    std::string_view line, std::size_t max_tokens = (std::numeric_limits<std::size_t>::max)());

[[nodiscard]] Tokens tokenize(std::string_view line,
                              std::size_t max_tokens = (std::numeric_limits<std::size_t>::max)());

// Перегрузка для временной строки удалена: результат смотрел бы на строку,
// разрушенную в конце полного выражения.
Tokens tokenize(std::string&&, std::size_t = 0) = delete;
Result<Tokens, ParseError> try_tokenize(std::string&&, std::size_t = 0) = delete;

}  // namespace taskq
