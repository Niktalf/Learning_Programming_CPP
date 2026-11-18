#pragma once

#include <memory>
#include <string>
#include <utility>

#include "chat/chat_store.hpp"
#include "chat/commands.hpp"
#include "chat/journal.hpp"

namespace chat {

// Сервер этапа 4: хранилище с реестром пользователей и необязательный журнал
// сообщений. Всё — поля, и всё освобождается деструктором Server.
//
// handle — единственная граница перехвата, как и на этапе 3: исключение
// любого вида превращается здесь в ответ клиенту. На этапе 15 handle будет
// вызывать сетевая сессия, на этапе 16 — обработчик HTTP-запроса.
class Server {
public:
    // Этап 9: зависимости — снаружи. Хранилище приходит готовым (данные,
    // ёмкость истории, часы), журнал — портом ILogSink (journal.hpp). Сервер
    // не знает ни файлов, ни путей, ни движков; собирает его точка сборки
    // make_server (compose.hpp). До этапа 9 конструкторы принимали путь и
    // имя движка и сами открывали файл (docs/audit.md, запись 4).
    //
    // Журнал — архив отправленных сообщений, строка «комнаты<TAB>автор<TAB>
    // текст», как логи IRC-канала; история при перезапуске из него не
    // восстанавливается.
    explicit Server(ChatStore store = ChatStore(), std::unique_ptr<ILogSink> log = nullptr)
        : store_(std::move(store)), log_(std::move(log)) {}

    // Вход и выход пользователя. Реестр владеет пользователем, пока тот в
    // чате; logout — это то, что на этапе 15 сделает отключение клиента.
    [[nodiscard]] Session login(std::string nick);
    void logout(const Session& session);

    // Каждая команда — и активность пользователя: сервер отмечает её в
    // хранилище до разбора (этап 9, /whois).
    [[nodiscard]] Response handle(Session& session, std::string line);

    [[nodiscard]] ChatStore& store() noexcept { return store_; }
    [[nodiscard]] const ChatStore& store() const noexcept { return store_; }

private:
    ChatStore store_;
    std::unique_ptr<ILogSink> log_;   // пусто — журнал не ведётся
};

}  // namespace chat
