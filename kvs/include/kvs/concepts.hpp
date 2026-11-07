#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kvs {

// Этап 5: требования к шаблонным параметрам записаны концепциями, а не
// закодированы трюками. Каждая проверяет не только наличие члена, но и тип
// результата: «метод есть» — половина требования.

// Ключ: std::string, std::string_view или строковый литерал. Число — нет.
//
// Почему не std::convertible_to<T, std::string_view>. Такая концепция
// пропустила бы const char* — указатель без длины, который может оказаться
// нулевым; string_view из нулевого указателя — неопределённое поведение.
// Литерал — другое дело: это массив известной длины. Поэтому массив char
// разрешён, указатель — нет. Тест Concepts.KeyLike проверяет и отказы.
template <class T>
concept KeyLike =
    std::same_as<std::remove_cvref_t<T>, std::string> ||
    std::same_as<std::remove_cvref_t<T>, std::string_view> ||
    (std::is_array_v<std::remove_cvref_t<T>> &&
     std::same_as<std::remove_cv_t<std::remove_extent_t<std::remove_cvref_t<T>>>, char>);

// Строковое представление ключа. Для литерала длина берётся из типа массива
// за вычетом завершающего нуля — без strlen и без доверия к указателю.
template <KeyLike K>
[[nodiscard]] constexpr std::string_view key_view(const K& key) noexcept {
    if constexpr (std::is_array_v<K>) {
        return std::string_view(key, std::extent_v<K> - 1);
    } else {
        return std::string_view(key);
    }
}

// Что хранилище требует от типа значения. Копирование нужно MSET (copy-and-
// swap этапа 3), неброс перемещения — вектору и обмену, size — учёту памяти.
template <class V>
concept StorableValue = std::copy_constructible<V> && std::is_nothrow_move_constructible_v<V> &&
                        requires(const V& value) {
                            { value.size() } -> std::convertible_to<std::size_t>;
                        };

// Сериализуемое значение: в байты и обратно. На этапе 17 на этом построится
// снапшот. Проверяются оба направления и оба типа результата.
template <class T>
concept Serializable = requires(const T& value, std::span<const std::byte> in) {
    { value.to_bytes() } -> std::convertible_to<std::vector<std::byte>>;
    { T::from_bytes(in) } -> std::same_as<T>;
};

}  // namespace kvs
