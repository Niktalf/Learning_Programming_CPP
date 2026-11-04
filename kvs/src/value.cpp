#include "kvs/value.hpp"

#include <type_traits>

namespace kvs {

// Проверки правила нуля выполняются на этапе компиляции: если кто-нибудь
// добавит в Value деструктор или собственный копирующий конструктор,
// сборка упадёт здесь, а не проявится потерей производительности.
//
// Записаны через ::value, а не через переменные-шаблоны с суффиксом _v:
// те появились в C++17, а мы пока в четырнадцатом.
static_assert(std::is_nothrow_move_constructible<Value>::value,
              "Value must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable<Value>::value,
              "Value must be nothrow move assignable");
static_assert(std::is_copy_constructible<Value>::value,
              "Value must be copy constructible");
static_assert(std::is_nothrow_destructible<Value>::value,
              "Value must be nothrow destructible");

}  // namespace kvs
