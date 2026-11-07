// Звёздочка этапа 6: повтор имени в StaticRegistry — ошибка компиляции.
// Регистр не важен, как и при разборе: «ACK» и «ack» — одна команда.
// Ожидаемая диагностика содержит текст static_assert «duplicate command name».

#include "taskq/static_registry.hpp"

namespace {

struct Tally {};

taskq::Response first(Tally&) {
    return {};
}

taskq::Response second(Tally&) {
    return {};
}

}  // namespace

int main() {
    const taskq::StaticRegistry<Tally, taskq::Command<"ACK", first>, taskq::Command<"ack", second>>
        commands;
    static_cast<void>(commands);
}
