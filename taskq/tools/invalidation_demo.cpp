// Домашнее задание этапа 7: обход очереди с удалением задач внутри цикла.
//
// Программа намеренно содержит неопределённое поведение и в обычный запуск
// не входит:
//
//   cmake --build build --target taskq_invalidation_demo
//   valgrind ./build/taskq_invalidation_demo
//   ./build/taskq_invalidation_debug        # отладочный режим libstdc++
//
// Очередь — std::map по ключу выдачи, как в BasicTaskStore. Задача — снять
// все задачи с низким приоритетом. Ошибка в одной строке: после erase(it)
// цикл делает ++it, а it указывает на удалённый узел дерева.
//
// Санитайзер адресов здесь молчит: инкремент итератора map — функция
// _Rb_tree_increment из libstdc++.so, собранной без санитайзера, и чтение
// освобождённого узла происходит в неинструментированном коде. Видят
// valgrind (вся память процесса) и отладочный режим libstdc++ (сам
// итератор). Вывод — в docs/iterators.md, исправление — в
// tests/test_invalidation.cpp.

#include <cstdint>
#include <iostream>
#include <map>
#include <string>

#include "taskq/task_key.hpp"

int main() {
    std::map<taskq::PlainTaskKey, std::string> queue;
    for (std::uint64_t seq = 1; seq <= 32; ++seq) {
        queue.emplace(taskq::PlainTaskKey{static_cast<int>(seq % 10), seq}, "task " + std::to_string(seq));
    }
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        if (it->first.priority < 5) {
            queue.erase(it);   // it теперь висит, а ++it в заголовке цикла его читает
        }
    }
    std::cout << "tasks left: " << queue.size() << '\n';
    return 0;
}
