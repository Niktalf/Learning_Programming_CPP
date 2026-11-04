#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "chat/chat_store.hpp"
#include "chat/executor.hpp"
#include "chat/log.hpp"

namespace chat {

// Сервер этапа 4: хранилище с реестром пользователей и необязательный журнал
// сообщений. Всё — поля, и всё освобождается деструктором Server.
//
// handle — единственная граница перехвата, как и на этапе 3: исключение
// любого вида превращается здесь в ответ клиенту. На этапе 15 handle будет
// вызывать сетевая сессия, на этапе 16 — обработчик HTTP-запроса.
class Server {
public:
    Server() = default;

    // С журналом: каждое отправленное сообщение дописывается в файл одной
    // строкой «комнаты<TAB>автор<TAB>текст». Это архив, как логи IRC-канала;
    // история при перезапуске из него не восстанавливается.
    //
    // Конструктор бросает, если файл не открылся. Деструктор Server тогда не
    // вызовется, но уже построенное хранилище разрушится корректно.
    explicit Server(const std::filesystem::path& log) : log_(std::in_place, log) {}

    // Вход и выход пользователя. Реестр владеет пользователем, пока тот в
    // чате; logout — это то, что на этапе 15 сделает отключение клиента.
    [[nodiscard]] Session login(std::string nick);
    void logout(const Session& session);

    [[nodiscard]] Response handle(Session& session, std::string line);

    [[nodiscard]] ChatStore& store() noexcept { return store_; }
    [[nodiscard]] const ChatStore& store() const noexcept { return store_; }

private:
    ChatStore store_;
    std::optional<AppendOnlyLog> log_;   // пусто — журнал не ведётся
};

}  // namespace chat
