#pragma once

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "kvs/errors.hpp"

namespace kvs::legacy {

// Этап 6: тот же convert, записанный так, как это делали до if constexpr и
// концепций, — набором перегрузок на std::enable_if. В проекте не
// используется; тест Convert.LegacyAgreesWithModern проверяет, что обе
// версии ведут себя одинаково. Сравнение — в docs/registry.md.
//
// Перегрузки обязаны быть взаимоисключающими: если для какого-то T условия
// двух сразу истинны, вызов неоднозначен. Поэтому каждое условие знает обо
// всех остальных. Чтобы добавить третий случай — optional, — пришлось
// исключить его в условиях двух первых. Добавление четвёртого — например,
// собственного числового типа — снова тронуло бы все три.

template <class T>
inline constexpr bool is_optional = false;

template <class T>
inline constexpr bool is_optional<std::optional<T>> = true;

template <class T>
inline constexpr bool is_string_like =
    std::is_same<T, std::string_view>::value || std::is_same<T, std::string>::value;

template <class T>
inline constexpr bool is_number =
    std::is_arithmetic<T>::value && !std::is_same<T, bool>::value && !is_optional<T>;

// Случай первый: строки.
template <class T>
typename std::enable_if<is_string_like<T> && !is_number<T> && !is_optional<T>, T>::type
convert(std::string_view token) {
    return T(token);
}

// Случай второй: числа. Условие повторяет исключения первого случая — иначе
// для T, подходящего под оба, вызов был бы неоднозначен.
template <class T>
typename std::enable_if<is_number<T> && !is_string_like<T> && !is_optional<T>, T>::type
convert(std::string_view token) {
    T value{};
    const char* const last = token.data() + token.size();
    const auto result = std::from_chars(token.data(), last, value);
    if (result.ec == std::errc::result_out_of_range) {
        throw ParseError("number out of range: " + std::string(token), std::string(token));
    }
    if (result.ec != std::errc{} || result.ptr != last) {
        throw ParseError("not a number: " + std::string(token), std::string(token));
    }
    return value;
}

// Случай третий: необязательный аргумент. Пустой токен — пустой optional.
template <class T>
typename std::enable_if<is_optional<T> && !is_string_like<T> && !is_number<T>, T>::type
convert(std::string_view token) {
    if (token.empty()) {
        return std::nullopt;
    }
    return convert<typename T::value_type>(token);
}

}  // namespace kvs::legacy
