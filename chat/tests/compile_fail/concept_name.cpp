// Не должно собираться. Проверяется тестом chat_concept_diagnostics_fail.
//
// Имя комнаты — число. Хранилище требует NameLike, и компилятор сообщает
// ровно это: какой вызов, какое ограничение не выполнено и почему. Сравните
// с выводом для enable_if_name.cpp — docs/concepts.md.

#include "chat/chat_store.hpp"

int main() {
    const chat::ChatStore store;
    return store.contains(42) ? 0 : 1;
}
