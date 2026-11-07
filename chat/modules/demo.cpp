// Потребитель модуля chat.utf8. Заголовки и import в одной единице
// трансляции: сначала #include, потом import — обратный порядок с GCC 15 не
// собирается, см. docs/modules.md.

#include <print>
#include <string_view>

import chat.utf8;

int main() {
    for (const std::string_view text : {std::string_view("hello"), std::string_view("привет"),
                                        std::string_view("\xd0")}) {
        std::println("{:?} is valid UTF-8 -> {}", text, chat::modules::is_valid_utf8(text));
    }
    return 0;
}
