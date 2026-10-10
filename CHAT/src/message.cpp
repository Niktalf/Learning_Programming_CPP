#include "message.hpp"

#include <type_traits>

static_assert(std::is_nothrow_move_constructible_v<Message>, "Message must be transmitted without exception");
static_assert(std::is_nothrow_move_assignable_v<Message>, "Message must be assigned by movement without exceptions");
static_assert(std::is_copy_constructible_v<Message>, "Message must be copied");
static_assert(std::is_nothrow_destructible_v<Message>, "Message must be destroyed without exception");

