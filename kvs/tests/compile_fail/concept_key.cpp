// Не должно собираться. Проверяется тестом kvs_concept_diagnostics_fail.
//
// Ключ — число. Хранилище требует KeyLike, и компилятор сообщает ровно это:
// какой вызов, какое ограничение не выполнено и почему. Сравните с выводом
// для enable_if_key.cpp — docs/concepts.md.

#include "kvs/store.hpp"

int main() {
    const kvs::Store store;
    return store.contains(42) ? 0 : 1;
}
