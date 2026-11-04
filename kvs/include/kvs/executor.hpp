#pragma once

#include <string>

#include "kvs/command.hpp"
#include "kvs/store.hpp"

namespace kvs {

// Результат успешной команды. Этап 3: неудача больше не кодируется флагом
// в ответе — она бросается исключением и ловится на единственной границе,
// в Server::handle. Поле ok остаётся, но его выставляет только граница.
struct Response {
    bool ok = true;
    std::string text;
};

// Исполняет команду и бросает при неудаче. Здесь ничего не ловится: внутри
// ядра только бросают.
[[nodiscard]] Response execute(Store& store, const Command& command);

// Меняет ли команда хранилище. Только такие команды пишутся в журнал.
[[nodiscard]] bool modifies(const Command& command) noexcept;

}  // namespace kvs
