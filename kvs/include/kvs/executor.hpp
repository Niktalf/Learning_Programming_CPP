#pragma once

#include <string>

#include "kvs/command.hpp"
#include "kvs/store.hpp"

namespace kvs {

// Ответ вместо кода возврата: на этапе 3 неудача станет исключением, и
// разница между «ошибка» и «результат» окончательно уедет в систему типов.
struct Response {
    bool ok = true;
    std::string text;
};

[[nodiscard]] Response execute(Store& store, const Command& command);

}  // namespace kvs
