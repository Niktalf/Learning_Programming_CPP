#include "task.hpp"

#include <type_traits>

static_assert(std::is_nothrow_move_constructible<Task>::value, "Task must be move-constructible without exceptions");
static_assert(std::is_nothrow_move_assignable<Task>::value, "Task must be assignable via move without exceptions");
static_assert(std::is_copy_constructible<Task>::value, "Task must be copy-constructible");
static_assert(std::is_nothrow_destructible<Task>::value, "Task must be destructible without exceptions");
