#include "kvs/value.hpp"

#include <type_traits>

namespace kvs {

// Свойства, на которых держатся хранилище и сильная гарантия MSET.
// Перемещение не бросает: std::vector и copy-and-swap полагаются на это.
static_assert(std::is_nothrow_move_constructible_v<Value>,
              "Value must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<Value>, "Value must be nothrow move assignable");
// Копирование есть, но бросать может: clone выделяет память.
static_assert(std::is_copy_constructible_v<Value>, "Value must be copy constructible");
static_assert(!std::is_nothrow_copy_constructible_v<Value>,
              "copying a Value allocates and may throw");
// Удаление через указатель на базу корректно только с виртуальным деструктором.
static_assert(std::has_virtual_destructor_v<IValue>, "IValue needs a virtual destructor");

}  // namespace kvs
