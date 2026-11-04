#include "chat/message.hpp"

#include <type_traits>

namespace chat {

// Проверки правила нуля на этапе компиляции: если кто-нибудь добавит в
// Message деструктор или собственный копирующий конструктор, сборка упадёт
// здесь, а не проявится потерей производительности на длинных сообщениях.
//
// Записаны через ::value: переменные-шаблоны с суффиксом _v появятся в C++17.
static_assert(std::is_nothrow_move_constructible<Message>::value,
              "Message must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable<Message>::value,
              "Message must be nothrow move assignable");
static_assert(std::is_copy_constructible<Message>::value,
              "Message must be copy constructible");
static_assert(std::is_nothrow_destructible<Message>::value,
              "Message must be nothrow destructible");

}  // namespace chat
