// Не должно собираться. Проверяется тестом kvs_sort_classic_fail.
//
// Сортировка ключей в std::list старым std::sort. Требование «итератор
// произвольного доступа» нигде не записано, оно лишь подразумевается
// реализацией: ошибка возникает глубоко внутри алгоритма, где итераторы
// вычитают друг из друга, и компилятор показывает всю цепочку инстанцирований.
// Сравните с sort_ranges.cpp — docs/concepts.md.

#include <algorithm>
#include <list>
#include <string>

int main() {
    std::list<std::string> keys = {"b", "a", "c"};
    std::sort(keys.begin(), keys.end());
    return 0;
}
