#include "message.hpp"

#include <type_traits>

static_assert(std::is_nothrow_move_constructible<Message>::value, "Message must be transmitted without exception");
static_assert(std::is_nothrow_move_assignable<Message>::value, "Message must be assigned by movement without exceptions");
static_assert(std::is_copy_constructible<Message>::value, "Message must be copied");
static_assert(std::is_nothrow_destructible<Message>::value, "Message must be destroyed without exception");

