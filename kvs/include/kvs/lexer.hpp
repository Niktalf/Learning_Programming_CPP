#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "kvs/errors.hpp"
#include "kvs/result.hpp"

namespace kvs {

// Токены — это представления на исходную строку, а не копии.
using Tokens = std::vector<std::string_view>;

// Этап 3: незакрытая кавычка больше не даёт пустой вектор, неотличимый от
// пустой строки, — она бросает ParseError с причиной.
[[nodiscard]] Tokens tokenize(std::string_view line);

// Второй путь: та же ошибка возвращается значением. Это основа обоих
// вариантов — tokenize лишь превращает ошибку в исключение. Лексер один,
// чтобы два пути разбора различались только обработкой ошибок, а не
// правилами разбиения строки.
[[nodiscard]] Result<Tokens, ParseError> try_tokenize(std::string_view line);

// Перегрузки для временной строки удалены: результат смотрел бы на строку,
// разрушенную в конце полного выражения.
Tokens tokenize(std::string&&) = delete;
Result<Tokens, ParseError> try_tokenize(std::string&&) = delete;

}  // namespace kvs
