// Домашнее задание этапа 7: обход контейнера с удалением внутри цикла.
//
// Программа намеренно содержит неопределённое поведение и в обычный запуск
// не входит. Два случая, и каждый ловит свой инструмент:
//
//   map    — std::unordered_map, основная таблица хранилища. После erase(it)
//            итератор it указывает в освобождённый узел, и ++it читает
//            освобождённую память. Ловит санитайзер адресов:
//
//              cmake --build build --target kvs_invalidation_demo
//              ./build/kvs_invalidation_demo map     # сборка с -fsanitize=address
//
//   deque  — std::deque, контейнер списка. erase в середине делает
//            недействительными все итераторы, но память, скорее всего, на
//            месте: санитайзер адресов молчит, программа «работает» и
//            выдаёт неверный результат. Ловит отладочный режим libstdc++
//            (-D_GLIBCXX_DEBUG): каждый итератор знает свой контейнер и
//            проверяет, не стал ли он недействительным. Эта сборка —
//            цель kvs_invalidation_debug, и тест ctest проверяет, что
//            отладочный режим ошибку нашёл.
//
// Вывод обоих инструментов — в docs/iterators.md. Исправление — идиома
// it = c.erase(it) — в tests/test_invalidation.cpp.

#include <cstdio>
#include <deque>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

void broken_map() {
    std::unordered_map<std::string, int> table;
    for (int i = 0; i < 64; ++i) {
        table.emplace("key:" + std::to_string(i), i);
    }
    // Удалить все чётные. Ошибка в одной строке: после erase(it) цикл делает
    // ++it — инкремент итератора удалённого узла.
    for (auto it = table.begin(); it != table.end(); ++it) {
        if (it->second % 2 == 0) {
            table.erase(it);
        }
    }
    std::cout << "map: " << table.size() << " left\n";
}

void broken_deque() {
    std::deque<int> items;
    for (int i = 0; i < 64; ++i) {
        items.push_back(i);
    }
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (*it % 2 == 0) {
            items.erase(it);   // все итераторы deque теперь недействительны
        }
    }
    std::cout << "deque: " << items.size() << " left\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string_view which = argc > 1 ? argv[1] : "";
    // Вывод отладочного режима и санитайзера идёт в stderr; stdout сбрасывается
    // заранее, чтобы строки не перемешались.
    std::cout.setf(std::ios::unitbuf);
    if (which == "map") {
        broken_map();
    } else if (which == "deque") {
        broken_deque();
    } else {
        std::cerr << "usage: kvs_invalidation_demo map|deque\n";
        return 2;
    }
    return 0;
}
