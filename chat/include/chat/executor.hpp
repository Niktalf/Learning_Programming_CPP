#pragma once

#include <string>

#include "chat/chat_store.hpp"
#include "chat/command.hpp"

namespace chat {

// Тот, от чьего имени исполняются команды. Пока это просто ник, занятый
// в реестре хранилища; на этапе 4 появятся объекты пользователей.
struct Session {
    std::string nick;
};

// Ответ клиенту. Неудача внутри ядра — исключение; ok == false появляется
// только на границе handle, где исключение превращается в ответ.
struct Response {
    bool ok = true;
    std::string text;
};

// Исполнение команды. Ошибки бросаются, каждая обёрнута в звено контекста
// «while executing …» (std::throw_with_nested). Сессия изменяемая: /nick
// меняет ник.
[[nodiscard]] Response execute(ChatStore& store, Session& session, const Command& command);

}  // namespace chat
