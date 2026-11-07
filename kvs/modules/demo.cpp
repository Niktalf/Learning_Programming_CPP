// Потребитель модуля kvs.glob. Заголовки и import в одной единице трансляции:
// сначала #include, потом import — обратный порядок с GCC 15 не собирается,
// см. docs/modules.md.

#include <print>

import kvs.glob;

int main() {
    for (const char* const key : {"user:1", "order:1", "user:10"}) {
        std::println("{} matches user:*  -> {}", key, kvs::modules::glob_match("user:*", key));
    }
    return 0;
}
