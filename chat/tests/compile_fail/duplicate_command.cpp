// Звёздочка этапа 6: повтор имени в StaticRegistry — ошибка компиляции.
// Регистр не важен, как и при разборе: «/bump» и «/BUMP» — одна команда.
// Ожидаемая диагностика содержит текст static_assert «duplicate command name».

#include "chat/static_registry.hpp"

namespace {

struct Tally {};

chat::Response first(Tally&) {
    return {};
}

chat::Response second(Tally&) {
    return {};
}

}  // namespace

int main() {
    const chat::StaticRegistry<Tally, chat::Command<"/bump", first>, chat::Command<"/BUMP", second>>
        commands;
    static_cast<void>(commands);
}
