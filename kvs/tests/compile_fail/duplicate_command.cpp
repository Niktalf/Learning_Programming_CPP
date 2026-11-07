// Звёздочка этапа 6: повтор имени в StaticRegistry — ошибка компиляции.
// Регистр не важен, как и при разборе: «BUMP» и «bump» — одна команда.
// Ожидаемая диагностика содержит текст static_assert «duplicate command name».

#include "kvs/static_registry.hpp"

namespace {

struct Tally {};

kvs::Response first(Tally&) {
    return {};
}

kvs::Response second(Tally&) {
    return {};
}

}  // namespace

int main() {
    const kvs::StaticRegistry<Tally, kvs::Command<"BUMP", first, kvs::Effect::Writes>,
                              kvs::Command<"bump", second, kvs::Effect::Writes>>
        commands;
    static_cast<void>(commands);
}
