#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace kvs {

// Значение теперь может быть строкой, целым или числом с плавающей точкой.
// std::variant, а не std::any: набор альтернатив известен на этапе компиляции,
// поэтому std::visit проверяет исчерпывающесть обработки, а значение хранится
// внутри объекта, без обязательной аллокации в куче.
using ValueData = std::variant<std::string, std::int64_t, double>;

// Правило нуля по-прежнему в силе: ни одной специальной функции не объявлено.
// variant копируется и перемещается ровно так же, как хранимая альтернатива.
class Value {
public:
    Value() = default;

    explicit Value(std::string data) noexcept : data_(std::move(data)) {}
    explicit Value(std::string_view data) : data_(std::string(data)) {}
    explicit Value(std::int64_t data) noexcept : data_(data) {}
    explicit Value(double data) noexcept : data_(data) {}

    [[nodiscard]] const ValueData& data() const noexcept { return data_; }

    // Имя текущей альтернативы для команды TYPE. switch по index() пришлось
    // бы править при добавлении альтернативы, и компилятор бы об этом
    // не сказал. Здесь исчерпывающесть проверяется на этапе компиляции.
    [[nodiscard]] std::string_view type_name() const noexcept;

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::size_t size() const noexcept;

    template <class T>
    [[nodiscard]] bool holds() const noexcept {
        return std::holds_alternative<T>(data_);
    }

    // Возвращает указатель, а не бросает: неверный тип здесь штатная
    // ситуация, и исполнитель сам превращает её в TypeMismatchError с
    // контекстом. std::get дал бы bad_variant_access без ключа и типов.
    template <class T>
    [[nodiscard]] const T* get_if() const noexcept {
        return std::get_if<T>(&data_);
    }

private:
    ValueData data_;
};

// Сравнение по-прежнему написано руками: генерация операторов по умолчанию
// появится только в C++20.
inline bool operator==(const Value& lhs, const Value& rhs) noexcept {
    return lhs.data() == rhs.data();
}

inline bool operator!=(const Value& lhs, const Value& rhs) noexcept {
    return !(lhs == rhs);
}

}  // namespace kvs
