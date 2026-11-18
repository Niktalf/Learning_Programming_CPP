// Домашнее задание этапа 7, исправление: удаление внутри обхода через
// it = c.erase(it). Ошибочная версия — tools/invalidation_demo.cpp.

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <unordered_map>

#include "kvs/ivalue.hpp"

TEST(Invalidation, EraseReturnsTheNextValidIterator) {
    // erase возвращает итератор на следующий элемент — действительный, в
    // отличие от того, который удалили. Инкремент — только в ветке без
    // удаления.
    std::unordered_map<std::string, int> table;
    for (int i = 0; i < 64; ++i) {
        table.emplace("key:" + std::to_string(i), i);
    }
    for (auto it = table.begin(); it != table.end();) {
        if (it->second % 2 == 0) {
            it = table.erase(it);
        } else {
            ++it;
        }
    }
    EXPECT_EQ(table.size(), 32u);
    for (const auto& [key, value] : table) {
        EXPECT_EQ(value % 2, 1) << key;
    }
}

TEST(Invalidation, SameIdiomOnDeque) {
    std::deque<int> items;
    for (int i = 0; i < 64; ++i) {
        items.push_back(i);
    }
    for (auto it = items.begin(); it != items.end();) {
        it = *it % 2 == 0 ? items.erase(it) : it + 1;
    }
    EXPECT_EQ(items.size(), 32u);
    EXPECT_EQ(items.front(), 1);
    EXPECT_EQ(items.back(), 63);
}

TEST(Invalidation, EraseIfIsTheShortWay) {
    // То же одной строкой — std::erase_if из C++20. Им и стоит пользоваться;
    // ручной цикл нужен, когда при удалении надо сделать что-то ещё.
    std::deque<int> items;
    for (int i = 0; i < 64; ++i) {
        items.push_back(i);
    }
    EXPECT_EQ(std::erase_if(items, [](int item) { return item % 2 == 0; }), 32u);
    EXPECT_EQ(items.size(), 32u);
}

TEST(Invalidation, DequeKeepsReferencesButNotIteratorsOnPushAtEnds) {
    // Правила std::deque, проверенные на деле там, где их можно проверить
    // без неопределённого поведения: адрес элемента — то, на чём держится
    // ссылка. Итераторы при той же вставке недействительны (их проверяет
    // отладочный режим, см. kvs_invalidation_debug), а ссылки — нет.
    std::deque<std::string> items = {"a", "b", "c"};
    const std::string* const b = &items[1];
    for (int i = 0; i < 1000; ++i) {
        items.push_front("x");
        items.push_back("y");
    }
    EXPECT_EQ(b, &items[1001]);
    // pop с конца делает недействительными только снятый элемент.
    items.pop_front();
    items.pop_back();
    EXPECT_EQ(b, &items[1000]);
}
