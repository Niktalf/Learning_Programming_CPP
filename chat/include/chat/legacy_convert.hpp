#pragma once

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "chat/convert.hpp"
#include "chat/errors.hpp"
#include "chat/validate.hpp"

namespace chat::legacy {

// Этап 6: тот же convert, записанный так, как это делали до if constexpr и
// концепций, — набором перегрузок на std::enable_if. В проекте не
// используется; тест Convert.LegacyAgreesWithModern проверяет, что обе
// версии ведут себя одинаково. Здесь три случая из шести — строка, число и
// RoomName, — и уже их хватает, чтобы увидеть, во что это обходится.
//
// Сравнение с версией на if constexpr:
//
//   - объём: три шаблона с условиями в типе результата против трёх веток
//     в одной функции; условия длиннее самого кода;
//   - читаемость: правило выбора размазано по трём сигнатурам, и чтобы
//     понять, какая перегрузка сработает для T, надо мысленно вычислить
//     все три условия;
//   - сообщение об ошибке для неподдерживаемого типа: «no matching function
//     for call to convert<bool>» и по примечанию на каждую перегрузку с её
//     условием — вместо одной строки с именем концепции ScalarArgument;
//   - четвёртый случай. Перегрузки обязаны быть взаимоисключающими: для T,
//     подходящего под два условия, вызов неоднозначен. Добавить Nick —
//     значит дописать новую перегрузку и исключить Nick в условиях всех
//     прежних. В версии на if constexpr — одна ветка, и порядок веток сам
//     задаёт предпочтение.

template <class T>
inline constexpr bool is_room = std::is_same<T, RoomName>::value;

template <class T>
inline constexpr bool is_string = std::is_same<T, std::string_view>::value;

template <class T>
inline constexpr bool is_number = std::is_arithmetic<T>::value && !std::is_same<T, bool>::value;

// Случай первый: строка как есть.
template <class T>
typename std::enable_if<is_string<T> && !is_number<T> && !is_room<T>, T>::type
convert(std::string_view token) {
    return token;
}

// Случай второй: число. Условие повторяет исключения первого случая.
template <class T>
typename std::enable_if<is_number<T> && !is_string<T> && !is_room<T>, T>::type
convert(std::string_view token) {
    T value{};
    const char* const last = token.data() + token.size();
    const auto result = std::from_chars(token.data(), last, value);
    if (result.ec == std::errc::result_out_of_range) {
        throw ParseError("number out of range: " + std::string(token), std::string(token));
    }
    if (result.ec != std::errc{} || result.ptr != last) {
        const char* const what =
            std::is_unsigned<T>::value ? "not a non-negative number: " : "not a number: ";
        throw ParseError(what + std::string(token), std::string(token));
    }
    return value;
}

// Случай третий: имя комнаты. Чтобы он появился, пришлось дописать
// !is_room<T> в условия двух первых.
template <class T>
typename std::enable_if<is_room<T> && !is_string<T> && !is_number<T>, T>::type
convert(std::string_view token) {
    if (const auto problem = room_name_problem(token)) {
        throw ParseError(*problem, std::string(token));
    }
    return RoomName{token};
}

}  // namespace chat::legacy
