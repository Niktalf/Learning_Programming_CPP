#pragma once

#include <charconv>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#include "chat/errors.hpp"
#include "chat/validate.hpp"

namespace chat {

// Этап 6: превращение токена команды в аргумент обработчика нужного типа.
//
// Типы-обёртки несут смысл, которого нет у string_view: RoomName — это уже
// проверенное имя комнаты, и обработчику не нужно проверять его заново.
// Проверка живёт в одном месте — в convert, — а не в каждой команде.
struct RoomName {
    std::string_view name;
};

// Список комнат /msg: «#a,#b,#c» — один токен, несколько проверенных имён без
// повторов.
struct RoomList {
    std::vector<std::string_view> rooms;
};

struct Nick {
    std::string_view name;
};

// Один токен текста — тема /topic. Многословная тема берётся в кавычки.
struct Text {
    std::string_view text;
};

// Маркер «весь остаток строки»: текст /msg. Остаток не разбивается на
// токены и не разбирает кавычки — «он сказал "привет» уходит как есть.
// Tail — всегда последний параметр обработчика; реестр проверяет это при
// компиляции и по нему решает, на сколько токенов резать строку.
struct Tail {
    std::string_view text;
};

template <class T>
inline constexpr bool is_optional_v = false;

template <class T>
inline constexpr bool is_optional_v<std::optional<T>> = true;

// Типы, которые умеет convert. Список закрытый: любой другой тип параметра
// обработчика — ошибка при регистрации команды, а не при её вызове.
template <class T>
concept ScalarArgument =
    std::same_as<T, std::string_view> || std::same_as<T, RoomName> || std::same_as<T, RoomList> ||
    std::same_as<T, Nick> || std::same_as<T, Text> || std::same_as<T, Tail> ||
    (std::is_arithmetic_v<T> && !std::same_as<T, bool>);

// Необязательным может быть любой скалярный аргумент, кроме Tail: остаток
// строки либо есть, либо команда без текста — это разные команды.
template <class T>
concept Argument = ScalarArgument<T> || (is_optional_v<T> && ScalarArgument<typename T::value_type> &&
                                         !std::same_as<typename T::value_type, Tail>);

// Разбор списка комнат. Не шаблон, поэтому живёт в src/convert.cpp.
[[nodiscard]] RoomList split_rooms(std::string_view list);

namespace detail {

[[noreturn]] inline void reject(const std::optional<std::string>& problem, std::string_view token) {
    throw ParseError(*problem, std::string(token));
}

}  // namespace detail

// Разбор одного токена. Ветки if constexpr проверяются по порядку, и порядок
// существен: специальные типы — раньше общих.
//
// У ParseError отсюда input — сам токен, а не вся строка: так точнее видно,
// что именно не разобралось. Какой была команда, допишет реестр.
template <ScalarArgument T>
[[nodiscard]] T convert(std::string_view token) {
    if constexpr (std::is_same_v<T, RoomName>) {
        if (const auto problem = room_name_problem(token); problem) {
            detail::reject(problem, token);
        }
        return RoomName{token};
    } else if constexpr (std::is_same_v<T, Nick>) {
        if (const auto problem = nick_problem(token); problem) {
            detail::reject(problem, token);
        }
        return Nick{token};
    } else if constexpr (std::is_same_v<T, Text> || std::is_same_v<T, Tail>) {
        if (const auto problem = text_problem(token); problem) {
            detail::reject(problem, token);
        }
        return T{token};
    } else if constexpr (std::is_same_v<T, RoomList>) {
        return split_rooms(token);
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        return token;
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
            // Для беззнакового типа «-1» — тоже не число, но сказать стоит
            // точнее: номер сообщения и предел не бывают отрицательными.
            const char* const what =
                std::is_unsigned_v<T> ? "not a non-negative number: " : "not a number: ";
            throw ParseError(what + std::string(token), std::string(token));
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

// Как аргумент выглядит в подсказке «usage: …». Тоже if constexpr по типу:
// подсказка выводится из сигнатуры обработчика, а не пишется руками, и
// разойтись с ней не может.
template <ScalarArgument T>
[[nodiscard]] constexpr std::string_view placeholder() noexcept {
    if constexpr (std::is_same_v<T, RoomName>) {
        return "#room";
    } else if constexpr (std::is_same_v<T, RoomList>) {
        return "#room[,#room...]";
    } else if constexpr (std::is_same_v<T, Nick>) {
        return "nick";
    } else if constexpr (std::is_same_v<T, Text>) {
        return "\"text\"";
    } else if constexpr (std::is_same_v<T, Tail>) {
        return "text...";
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        return "word";
    } else {
        return "number";
    }
}

}  // namespace chat
