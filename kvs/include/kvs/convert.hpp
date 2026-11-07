#pragma once

#include <charconv>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "kvs/errors.hpp"

namespace kvs {

// Этап 6: превращение токена команды в аргумент обработчика нужного типа.
//
// Маркер «все оставшиеся токены». Нужен MSET: пар ключ–значение сколько
// угодно, и фиксированной арностью их не выразить. Rest — всегда последний
// параметр обработчика; реестр проверяет это при компиляции.
struct Rest {
    std::span<const std::string_view> tokens;
};

template <class T>
inline constexpr bool is_optional_v = false;

template <class T>
inline constexpr bool is_optional_v<std::optional<T>> = true;

// Типы, которые умеет convert. Список закрытый: любой другой тип параметра
// обработчика — ошибка при регистрации команды, а не при её вызове.
template <class T>
concept ScalarArgument = std::same_as<T, std::string_view> || std::same_as<T, std::string> ||
                         (std::is_arithmetic_v<T> && !std::same_as<T, bool>);

template <class T>
concept Argument = ScalarArgument<T> || std::same_as<T, Rest> ||
                   (is_optional_v<T> && ScalarArgument<typename T::value_type>);

// Разбор одного токена. Ветки if constexpr проверяются по порядку, и порядок
// существен: специальные типы — раньше общих. Если бы ветка is_arithmetic_v
// стояла выше ветки для какого-то собственного числового типа, до неё
// очередь бы не дошла.
//
// bool исключён явно: from_chars его не разбирает, а is_arithmetic_v<bool>
// истинно, и без исключения ветка для чисел молча приняла бы bool.
template <ScalarArgument T>
[[nodiscard]] T convert(std::string_view token) {
    if constexpr (std::is_same_v<T, std::string_view>) {
        return token;
    } else if constexpr (std::is_same_v<T, std::string>) {
        return std::string(token);
    } else if constexpr (std::is_arithmetic_v<T>) {
        T value{};
        const char* const last = token.data() + token.size();
        const auto [end, error] = std::from_chars(token.data(), last, value);
        // Проверка end != last существенна: без неё «12abc» разобралось бы как
        // 12 — from_chars разбирает префикс и сообщает об успехе.
        if (error == std::errc::result_out_of_range) {
            throw ParseError("number out of range: " + std::string(token), std::string(token));
        }
        if (error != std::errc{} || end != last) {
            throw ParseError("not a number: " + std::string(token), std::string(token));
        }
        return value;
    } else {
        // static_assert(false) в отброшенной ветке законен только с C++23
        // (P2593). Раньше он срабатывал при разборе шаблона, даже если ветка
        // не выбиралась, и приходилось писать зависимое от T выражение вроде
        // sizeof(T) == 0. Сюда концепция ScalarArgument и так не пропустит.
        static_assert(false, "convert: unsupported argument type");
    }
}

}  // namespace kvs
