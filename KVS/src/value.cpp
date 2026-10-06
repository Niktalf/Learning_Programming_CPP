#include "value.hpp"

#include <type_traits>

static_assert(std::is_nothrow_move_constructible_v<Value>, "Value must be moved without exceptions");
static_assert(std::is_nothrow_move_assignable_v<Value>, "Value must be assigned by movement without exceptions");
static_assert(std::is_copy_constructible_v<Value>, "Value must be copied");
static_assert(std::is_nothrow_destructible_v<Value>, "Value must be destroyed without exception");
