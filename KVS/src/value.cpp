#include "value.hpp"

#include <type_traits>

static_assert(std::is_nothrow_move_constructible<Value>::value, "Value must be moved without exceptions");
static_assert(std::is_nothrow_move_assignable<Value>::value, "Value must be assigned by movement without exceptions");
static_assert(std::is_copy_constructible<Value>::value, "Value must be copied");
static_assert(std::is_nothrow_destructible<Value>::value, "Value must be destroyed without exception");

