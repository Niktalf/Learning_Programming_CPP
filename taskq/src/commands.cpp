#include "taskq/registry.hpp"
#include "taskq/service.hpp"

namespace taskq {

// Этап 6: все команды сервиса — в одном реестре, по строке на команду.
// Обработчики — методы Service: так у них есть доступ к закрытому состоянию,
// а реестр вызывает метод через std::invoke так же, как свободную функцию.
// Константный метод — команда, которая только читает.
const CommandRegistry<Service>& Service::commands() {
    // Реестр строится один раз, при первом обращении; инициализация
    // локальной статической переменной потокобезопасна по стандарту.
    static const CommandRegistry<Service> registry = [] {
        CommandRegistry<Service> table;
        table.register_command("CREATE", &Service::create)
            .register_command("SUBMIT", &Service::submit)
            .register_command("SUBMITBATCH", &Service::submit_batch)
            .register_command("POLL", &Service::poll)
            .register_command("ACK", &Service::ack)
            .register_command("FAIL", &Service::fail)
            .register_command("RELEASE", &Service::release)
            .register_command("STATUS", &Service::status)
            .register_command("TYPE", &Service::type_of)
            .register_command("BUMP", &Service::bump)
            .register_command("WORKER", &Service::add_worker)
            .register_command("GONE", &Service::gone)
            .register_command("REAP", &Service::reap)
            .register_command("LIST", &Service::list)
            .register_command("DELAY", &Service::delay)
            .register_command("PURGE", &Service::purge)
            .register_command("PEEK", &Service::peek);
        return table;
    }();
    return registry;
}

}  // namespace taskq
