#pragma once

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "taskq/convert.hpp"
#include "taskq/errors.hpp"
#include "taskq/task.hpp"

namespace taskq::legacy {

// Этап 6: тот же convert, записанный так, как это делали до if constexpr и
// концепций, — набором перегрузок на std::enable_if. В проекте не
// используется; тест Convert.LegacyAgreesWithModern проверяет, что обе
// версии ведут себя одинаково. Здесь четыре случая из одиннадцати —
// строка, Priority, TaskId и прочие числа.
//
// Сравнение с версией на if constexpr:
//
//   - объём: условия в типах результата длиннее самого кода;
//   - читаемость: правило выбора размазано по четырём сигнатурам, и чтобы
//     понять, какая перегрузка сработает для T, надо мысленно вычислить все
//     четыре условия;
//   - сообщение об ошибке для неподдерживаемого типа: «no matching function
//     for call to convert<bool>» и по примечанию на каждую перегрузку с её
//     условием — вместо одной строки с именем концепции ScalarArgument;
//   - пятый случай. Перегрузки обязаны быть взаимоисключающими: для T,
//     подходящего под два условия, вызов неоднозначен. Порядка, как у веток
//     if constexpr, у перегрузок нет, поэтому «TaskId раньше чисел» здесь
//     выражено вычитанием: условие для чисел содержит !is_task_id<T>.
//     Добавить пятый случай, скажем QueueName, — новая перегрузка и
//     исключение QueueName в условиях всех четырёх прежних.

template <class T>
inline constexpr bool is_string = std::is_same<T, std::string_view>::value;

template <class T>
inline constexpr bool is_priority = std::is_same<T, Priority>::value;

template <class T>
inline constexpr bool is_task_id = std::is_same<T, TaskId>::value;

template <class T>
inline constexpr bool is_number = std::is_arithmetic<T>::value && !std::is_same<T, bool>::value;

// Случай первый: строка как есть.
template <class T>
typename std::enable_if<is_string<T> && !is_priority<T> && !is_task_id<T> && !is_number<T>, T>::type
convert(std::string_view token) {
    return token;
}

// Случай второй: приоритет с проверкой диапазона.
template <class T>
typename std::enable_if<is_priority<T> && !is_string<T> && !is_task_id<T> && !is_number<T>, T>::type
convert(std::string_view token) {
    int value = 0;
    const char* const last = token.data() + token.size();
    const auto result = std::from_chars(token.data(), last, value);
    if (result.ec != std::errc{} || result.ptr != last || value < 0 || value > max_priority) {
        throw ParseError("priority must be 0.." + std::to_string(max_priority) + ": " + std::string(token),
                         std::string(token));
    }
    return Priority{value};
}

// Случай третий: номер задачи. TaskId — арифметический тип, поэтому условие
// случая четвёртого обязано его исключить, иначе вызов неоднозначен.
template <class T>
typename std::enable_if<is_task_id<T> && !is_string<T> && !is_priority<T>, T>::type
convert(std::string_view token) {
    const bool digits_only = !token.empty() && token.front() != '0' &&
                             token.find_first_not_of("0123456789") == std::string_view::npos;
    T value{};
    const char* const last = token.data() + token.size();
    const auto result = std::from_chars(token.data(), last, value);
    if (!digits_only || result.ec != std::errc{} || result.ptr != last) {
        throw ParseError("task id must be a positive number: " + std::string(token), std::string(token));
    }
    return value;
}

// Случай четвёртый: прочие числа.
template <class T>
typename std::enable_if<is_number<T> && !is_task_id<T> && !is_string<T> && !is_priority<T>, T>::type
convert(std::string_view token) {
    T value{};
    const char* const last = token.data() + token.size();
    const auto result = std::from_chars(token.data(), last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        throw ParseError("not a number: " + std::string(token), std::string(token));
    }
    return value;
}

}  // namespace taskq::legacy
