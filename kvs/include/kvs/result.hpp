#pragma once

#include <type_traits>
#include <utility>
#include <variant>

namespace kvs {

// Второй путь обработки ошибок — возврат ошибки через тип.
//
// В C++23 для этого есть std::expected<T, E>. Проект на этапе 3 собирается под
// C++17, поэтому здесь его минимальная собственная версия поверх std::variant:
// внутри лежит либо результат, либо ошибка. На этапе 5, с переходом на C++23,
// её можно заменить на std::expected почти без изменений в вызывающем коде —
// интерфейс намеренно повторяет стандартный.
//
// Отличие от стандарта одно и осознанное: value() при ошибке бросает саму
// ошибку E, а не std::bad_expected_access<E>. Так Result и исключения
// стыкуются без перевода: разбор на исключениях — это try_parse(...).value().

// Обёртка, отличающая ошибку от значения при конструировании. Без неё
// Result<std::string, std::string> не понял бы, что ему передали.
template <class E>
struct Unexpected {
    E error;
};

template <class E>
Unexpected(E) -> Unexpected<E>;

template <class T, class E>
class Result {
public:
    // Неявные конструкторы — как у std::expected: функция пишет return value;
    // или return Unexpected{error};, не повторяя тип результата.
    Result(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : data_(std::in_place_index<0>, std::move(value)) {}

    Result(Unexpected<E> failure) noexcept(std::is_nothrow_move_constructible_v<E>)
        : data_(std::in_place_index<1>, std::move(failure.error)) {}

    [[nodiscard]] bool has_value() const noexcept { return data_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] const T& value() const& {
        if (!has_value()) {
            throw error();
        }
        return std::get<0>(data_);
    }

    [[nodiscard]] T&& value() && {
        if (!has_value()) {
            throw error();
        }
        return std::get<0>(std::move(data_));
    }

    // Обращение к ошибке при наличии значения — ошибка программиста, как и у
    // std::expected. std::get здесь бросит std::bad_variant_access.
    [[nodiscard]] const E& error() const& { return std::get<1>(data_); }

    [[nodiscard]] const T& operator*() const& { return std::get<0>(data_); }
    [[nodiscard]] const T* operator->() const { return &std::get<0>(data_); }

private:
    std::variant<T, E> data_;
};

}  // namespace kvs
