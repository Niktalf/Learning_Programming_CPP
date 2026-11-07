// Не должно собираться. Проверяется тестом taskq_concept_diagnostics_fail.
//
// Имя очереди — число. Хранилище требует NameLike, и компилятор сообщает
// ровно это: какой вызов, какое ограничение не выполнено и почему. Сравните
// с выводом для enable_if_name.cpp — docs/concepts.md.

#include "taskq/task_store.hpp"

int main() {
    const taskq::TaskStore store;
    return store.contains(42) ? 0 : 1;
}
