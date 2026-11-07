// Этап 6: регистрация обработчика, который не подходит реестру. Параметр
// bool convert не умеет — bool из from_chars не разбирается, — и концепция
// CommandHandler отвергает обработчик при компиляции, в строке add, а не при
// первом вызове команды.
//
// Ожидаемая диагностика называет невыполненное требование:
// «ArgumentsSupported» внутри CommandHandler.

#include "kvs/registry.hpp"
#include "kvs/store.hpp"

namespace {

kvs::Response toggle(kvs::Store&, bool) {
    return {};
}

}  // namespace

int main() {
    kvs::CommandRegistry<kvs::Store> table;
    table.add("TOGGLE", kvs::Effect::Writes, toggle);
}
