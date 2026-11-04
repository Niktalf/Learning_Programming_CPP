#pragma once

#include <string>

#include "chat/chat_store.hpp"
#include "chat/command.hpp"

namespace chat {

// Тот, от чьего имени исполняются команды. Пока это просто ник: на этапе 3
// появится реестр ников, на этапе 4 — объекты пользователей.
struct Session {
    std::string nick;
};

// Ответ вместо кода возврата: на этапе 3 неудача станет исключением, и
// разница между «ошибка» и «результат» окончательно уедет в систему типов.
struct Response {
    bool ok = true;
    std::string text;
};

[[nodiscard]] Response execute(ChatStore& store, const Session& session,
                               const Command& command);

}  // namespace chat
