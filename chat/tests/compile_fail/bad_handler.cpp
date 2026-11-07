// Этап 6: регистрация обработчика, который не подходит реестру. Имя комнаты
// принято как const char* — convert такого типа не знает, — и концепция
// CommandHandler отвергает обработчик при компиляции, в строке
// register_command, а не при первом вызове команды.
//
// Ожидаемая диагностика называет невыполненное требование:
// «ArgumentsSupported» внутри CommandHandler.

#include "chat/commands.hpp"

namespace {

chat::Response join(chat::Context&, const char*) {
    return {};
}

}  // namespace

int main() {
    chat::CommandRegistry<chat::Context> table;
    table.register_command("/join", join);
}
