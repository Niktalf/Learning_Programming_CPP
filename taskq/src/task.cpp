#include "taskq/task.hpp"

#include <type_traits>

namespace taskq {

// Проверки правила нуля на этапе компиляции: если кто-нибудь добавит в Task
// деструктор или собственный копирующий конструктор, сборка упадёт здесь,
// а не проявится потерей производительности на больших телах задач.
//
// Записаны через ::value: переменные-шаблоны с суффиксом _v появятся в C++17.
static_assert(std::is_nothrow_move_constructible<Task>::value,
              "Task must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable<Task>::value,
              "Task must be nothrow move assignable");
static_assert(std::is_copy_constructible<Task>::value,
              "Task must be copy constructible");
static_assert(std::is_nothrow_destructible<Task>::value,
              "Task must be nothrow destructible");

}  // namespace taskq
