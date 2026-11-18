// Домашнее задание этапа 7: обход участников комнаты с удалением внутри
// цикла.
//
// Программа намеренно содержит неопределённое поведение и в обычный запуск
// не входит:
//
//   cmake --build build --target chat_invalidation_demo
//   valgrind ./build/chat_invalidation_demo
//   ./build/chat_invalidation_debug        # отладочный режим libstdc++
//
// Состав комнаты — std::set<std::weak_ptr<User>, std::owner_less<>>, как в
// BasicChatStore::Members. Задача — убрать ушедших, то есть истёкшие
// weak_ptr. Ошибка в одной строке: после erase(it) цикл делает ++it, а it
// указывает на удалённый узел дерева.
//
// Неожиданность: санитайзер адресов здесь молчит. Инкремент итератора set —
// функция _Rb_tree_increment, и она живёт не в заголовке, а в
// libstdc++.so, собранной без санитайзера: чтение освобождённого узла
// происходит в неинструментированном коде. valgrind следит за всей
// памятью процесса и ошибку видит; отладочный режим libstdc++ проверяет
// сам итератор. Вывод всех трёх — в docs/iterators.md.
//
// Исправление — it = members.erase(it), а короче — std::erase_if; так и
// сделано в BasicChatStore::purge, тест — tests/test_invalidation.cpp.

#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "chat/user.hpp"

int main() {
    std::vector<std::shared_ptr<chat::User>> users;
    std::set<std::weak_ptr<chat::User>, std::owner_less<>> members;
    for (int i = 0; i < 16; ++i) {
        users.push_back(std::make_shared<chat::User>("user" + std::to_string(i)));
        members.insert(users.back());
    }
    // Чётные ушли: реестр их больше не держит, weak_ptr истекли.
    for (std::size_t i = 0; i < users.size(); i += 2) {
        users[i].reset();
    }
    for (auto it = members.begin(); it != members.end(); ++it) {
        if (it->expired()) {
            members.erase(it);   // it теперь висит, а ++it в заголовке цикла его читает
        }
    }
    std::cout << "members left: " << members.size() << '\n';
    return 0;
}
