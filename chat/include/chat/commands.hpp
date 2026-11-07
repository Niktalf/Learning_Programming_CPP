#pragma once

#include <string>
#include <string_view>

#include "chat/chat_store.hpp"
#include "chat/log.hpp"
#include "chat/registry.hpp"

namespace chat {

// Тот, от чьего имени исполняются команды. Пока это просто ник, занятый
// в реестре хранилища; на этапе 15 сессия станет сетевым соединением.
struct Session {
    std::string nick;
};

// Контекст исполнения команды: всё, с чем работает обработчик. Ссылки, а
// не владение: хранилищем и журналом владеет Server, сессией — тот, кто
// вызвал handle. Журнал необязателен, поэтому указатель: nullptr — журнал не
// ведётся. Указатель не владеющий, и удалять по нему некому и незачем.
struct Context {
    ChatStore& store;
    Session& session;
    AppendOnlyLog* journal = nullptr;
};

// Этап 6: все команды чата — в одном реестре. Добавить команду — одна
// строка регистрации в src/commands.cpp и функция-обработчик рядом с ней.
[[nodiscard]] const CommandRegistry<Context>& commands();

// Разобрать и сразу исполнить одну строку. Строка живёт всё время вызова,
// поэтому временная здесь безопасна. Используют тесты; сервер вызывает
// commands().bind сам, со своим журналом.
[[nodiscard]] Response execute(ChatStore& store, Session& session, std::string_view line);

constexpr std::size_t default_history_limit = 10;

}  // namespace chat
