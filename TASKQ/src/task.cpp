#include "task.hpp"

#include <type_traits>

static_assert(std::is_nothrow_move_constructible_v<Task>, "Task must be move-constructible without exceptions");
static_assert(std::is_nothrow_move_assignable_v<Task>, "Task must be assignable via move without exceptions");
static_assert(std::is_copy_constructible_v<Task>, "Task must be copy-constructible");
static_assert(std::is_nothrow_destructible_v<Task>, "Task must be destructible without exceptions");
