// Учебная демонстрация: так делать нельзя.
//
// В обычную сборку не входит. Собирается отдельной целью и запускается под
// санитайзером адресов, чтобы своими глазами увидеть обращение к
// освобождённой памяти:
//
//     cmake --build build --target chat_dangling_demo && ./build/chat_dangling_demo
//
// Вывод санитайзера (GCC 13, сокращён до сути):
//
//   ERROR: AddressSanitizer: heap-use-after-free on address 0x504000000010
//   READ of size 4 at 0x504000000010 thread T0
//       #3 in main tests/dangling_demo.cpp:50          <- печать tokens.front()
//   0x504000000010 is located 0 bytes inside of 46-byte region
//   freed by thread T0 here:
//       #1 in main tests/dangling_demo.cpp:45          <- конец блока, ~string
//   previously allocated by thread T0 here:
//       #3 in main tests/dangling_demo.cpp:43          <- const std::string line
//   SUMMARY: AddressSanitizer: heap-use-after-free
//
// Три стека рассказывают всю историю: где память выделена (строка), где
// освобождена (выход из блока) и где к ней обратились после этого (токен).
// Строка нарочно длинная: короткая уместилась бы во внутренний буфер
// std::string, лежала бы на стеке, и санитайзер кучи ничего бы не увидел —
// ошибка осталась бы, но стала бы тихой.
//
// Исправление — в архитектуре, а не здесь: разбор принимает InputLine, который
// нельзя ни переместить, ни передать временным, а tokenize(std::string&&)
// удалена. Тесты Lifetime.* проверяют это на этапе компиляции.

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "chat/lexer.hpp"

int main() {
    std::vector<std::string_view> tokens;

    {
        // Строка живёт только внутри этого блока...
        const std::string line = "/msg #general long-enough-to-allocate-on-heap";
        tokens = chat::tokenize(line);
    }
    // ...а токены пережили её. Дальше — обращение к освобождённой памяти.

    std::cout << "tokens: " << tokens.size() << '\n';
    if (!tokens.empty()) {
        std::cout << "first token: " << tokens.front() << '\n';   // здесь всё и рушится
    }
    return 0;
}
