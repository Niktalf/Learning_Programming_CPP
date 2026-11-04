// Звёздочка этапа 2: сколько аллокаций стоит разбор тысячи команд лексером на
// std::string и лексером на std::string_view.
//
//   cmake --build build --target taskq_parse_allocations && ./build/taskq_parse_allocations
//
// Результат (GCC 13, libstdc++, сборка RelWithDebInfo):
//
//   lexer on std::string:      2668 allocations for 1000 commands (2334 tokens)
//   lexer on std::string_view: 2334 allocations for 1000 commands (2334 tokens)
//
// Откуда берутся числа.
//
// Лексер на string_view платит только за вектор токенов: он растёт 1 → 2 → 4,
// то есть две аллокации на команду из двух токенов и три — из трёх. Совпадение
// итога с числом токенов здесь не случайно, а следствие этой арифметики.
// Сами токены — пара «указатель, длина» внутри исходной строки, им память
// не нужна. Хвост SUBMIT — такое же представление, только длиннее.
//
// Лексер на std::string платит за то же самое плюс за каждый токен длиннее
// 15 символов: короткие строки хранятся внутри объекта std::string, длинные —
// в куче. В нашей нагрузке длинное только тело SUBMIT, поэтому разница —
// по одной аллокации на каждую третью команду. На длинных именах очередей
// разница выросла бы кратно.
//
// Вывод: string_view убирает аллокации на токены, но не на контейнер. Если и
// он мешает, вектор можно переиспользовать между вызовами (clear вместо
// создания нового) — тогда разбор обходится вовсе без обращений к куче.

#include <iostream>
#include <string>
#include <vector>

#include "alloc_counter.hpp"
#include "taskq/lexer.hpp"
#include "naive_lexer.hpp"
#include "workload.hpp"

int main() {
    if (!taskq::tools::counting_available()) {
        std::cout << "allocation counting is disabled under AddressSanitizer\n";
        return 0;
    }
    const std::vector<std::string> lines = taskq::tools::sample_commands();

    taskq::tools::reset_allocations();
    std::size_t copied_tokens = 0;
    for (const std::string& line : lines) {
        copied_tokens += taskq::tools::tokenize_copying(line, taskq::tools::limit_for(line)).size();
    }
    const std::size_t copying = taskq::tools::allocations();

    taskq::tools::reset_allocations();
    std::size_t viewed_tokens = 0;
    for (const std::string& line : lines) {
        viewed_tokens += taskq::tokenize(line, taskq::tools::limit_for(line)).size();
    }
    const std::size_t viewing = taskq::tools::allocations();

    std::cout << "lexer on std::string:      " << copying << " allocations for "
              << lines.size() << " commands (" << copied_tokens << " tokens)\n"
              << "lexer on std::string_view: " << viewing << " allocations for "
              << lines.size() << " commands (" << viewed_tokens << " tokens)\n";
    return 0;
}
