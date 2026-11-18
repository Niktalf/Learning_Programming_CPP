#include "taskq/task.hpp"

#include <type_traits>

namespace taskq {

// Свойства, на которых держатся очереди и сильная гарантия пакетной
// постановки. Перемещение не бросает: std::vector и POLL полагаются на это.
static_assert(std::is_nothrow_move_constructible_v<Task>, "Task must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<Task>, "Task must be nothrow move assignable");
// Копирование есть, но бросать может: clone выделяет память.
static_assert(std::is_copy_constructible_v<Task>, "Task must be copy constructible");
static_assert(!std::is_nothrow_copy_constructible_v<Task>, "copying a Task allocates and may throw");

}  // namespace taskq
