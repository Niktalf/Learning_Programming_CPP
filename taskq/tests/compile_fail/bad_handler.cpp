// Этап 6: регистрация обработчика, который не подходит реестру. Приоритет
// принят как bool — convert такого типа не знает: is_arithmetic_v<bool>
// истинно, но from_chars bool не разбирает, и концепция ScalarArgument
// исключает его явно. CommandHandler отвергает обработчик при компиляции, в
// строке register_command, а не при первом вызове команды.
//
// Ожидаемая диагностика называет невыполненное требование:
// «ArgumentsSupported» внутри CommandHandler.

#include "taskq/service.hpp"

namespace {

taskq::Response urgent(taskq::Service&, taskq::QueueName, bool) {
    return {};
}

}  // namespace

int main() {
    taskq::CommandRegistry<taskq::Service> table;
    table.register_command("URGENT", urgent);
}
