#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace chat {

// Этап 5: требования к шаблонным параметрам записаны концепциями, а не
// закодированы трюками. Каждая проверяет не только наличие члена, но и тип
// результата: «метод есть» — половина требования.

// Имя комнаты или ник: std::string, std::string_view или строковый литерал.
// Число — нет.
//
// Почему не std::convertible_to<T, std::string_view>. Такая концепция
// пропустила бы const char* — указатель без длины, который может оказаться
// нулевым; string_view из нулевого указателя — неопределённое поведение.
// Литерал — массив известной длины. Поэтому массив char разрешён, указатель
// — нет. Тест Concepts.NameLike проверяет и отказы.
template <class T>
concept NameLike =
    std::same_as<std::remove_cvref_t<T>, std::string> ||
    std::same_as<std::remove_cvref_t<T>, std::string_view> ||
    (std::is_array_v<std::remove_cvref_t<T>> &&
     std::same_as<std::remove_cv_t<std::remove_extent_t<std::remove_cvref_t<T>>>, char>);

// Строковое представление имени. Для литерала длина берётся из типа массива
// за вычетом завершающего нуля — без strlen и без доверия к указателю.
template <NameLike N>
[[nodiscard]] constexpr std::string_view name_view(const N& name) noexcept {
    if constexpr (std::is_array_v<N>) {
        return std::string_view(name, std::extent_v<N> - 1);
    } else {
        return std::string_view(name);
    }
}

// Сериализуемое значение: в байты и обратно. На этапе 17 на этом построится
// сохранение истории. Проверяются оба направления и оба типа результата.
template <class T>
concept Serializable = requires(const T& value, std::span<const std::byte> in) {
    { value.to_bytes() } -> std::convertible_to<std::vector<std::byte>>;
    { T::from_bytes(in) } -> std::same_as<T>;
};

}  // namespace chat
