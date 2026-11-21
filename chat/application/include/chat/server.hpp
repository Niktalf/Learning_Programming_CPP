#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "chat/chat_store.hpp"
#include "chat/commands.hpp"
#include "chat/events.hpp"
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
    //
    // Этап 10: и издатель доменных событий — порт IEventPublisher
    // (events.hpp). Сервер забирает события хранилища после каждой команды,
    // входа и выхода и публикует их.
    explicit Server(ChatStore store = ChatStore(), std::unique_ptr<ILogSink> log = nullptr,
                    std::unique_ptr<IEventPublisher> events = nullptr)
        : store_(std::move(store)), log_(std::move(log)), events_(std::move(events)) {
        store_.record_events(events_ != nullptr);
    }

    // Вход и выход пользователя. Реестр владеет пользователем, пока тот в
    // чате; logout — это то, что на этапе 15 сделает отключение клиента.
    [[nodiscard]] Session login(std::string nick);
    void logout(const Session& session);

    // Каждая команда — и активность пользователя: сервер отмечает её в
    // хранилище до разбора (этап 9, /whois).
    [[nodiscard]] Response handle(Session& session, std::string line);

    [[nodiscard]] ChatStore& store() noexcept { return store_; }
    [[nodiscard]] const ChatStore& store() const noexcept { return store_; }

    // Сколько событий не удалось опубликовать: издатель бросил. Команда при
    // этом выполнена — сообщение в комнате есть (docs/events.md).
    [[nodiscard]] std::size_t lost_events() const noexcept { return lost_events_; }

private:
    ChatStore store_;
    std::unique_ptr<ILogSink> log_;   // пусто — журнал не ведётся
    std::unique_ptr<IEventPublisher> events_;   // этап 10: пусто — события не публикуются
    std::size_t lost_events_ = 0;

    // Опубликовать всё, что хранилище накопило. Не бросает: событие, которое
    // не ушло, считается потерянным.
    void publish_events() noexcept;
};

}  // namespace chat
