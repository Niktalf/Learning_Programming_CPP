#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kvs {

// Токены — это представления на исходную строку, а не копии. Ни одного
// std::string внутри: разбор тысячи команд не должен стоить тысячи аллокаций.
//
// Поддерживаются кавычки: SET greeting "hello world" даёт три токена.
using Tokens = std::vector<std::string_view>;

[[nodiscard]] Tokens tokenize(std::string_view line);

// Перегрузка для временной строки удалена намеренно. Без неё вызов
// tokenize(std::string("...")) компилировался бы и возвращал представления
// на строку, разрушенную в конце полного выражения. Компилятор такую ошибку
// сам не ловит, поэтому мы закрываем её на уровне интерфейса.
Tokens tokenize(std::string&&) = delete;

}  // namespace kvs
