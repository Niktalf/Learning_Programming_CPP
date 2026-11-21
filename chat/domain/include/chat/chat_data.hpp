#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "chat/engine_kind.hpp"
#include "chat/user.hpp"

namespace chat {

// Этап 9: интерфейс данных чата принадлежит потребителю — хранилищу, — и
// лежит отдельно от реализаций. До этапа 9 он был в engine.hpp вместе со
// StdChatStore, TableChatStore и хеш-таблицами, и chat_store.hpp, включая
// интерфейс, получал и все реализации (DIP, docs/audit.md, запись 2).
// Теперь chat_store.hpp включает только этот заголовок, а реализации — в
// engines.hpp.
// Этап 8: данные чата за интерфейсом.
//
// IChatStore — то, что хранилище чата держит в таблицах: индекс комнат (имя
// → комната) и реестр ников (ник → пользователь). Вся логика — участники,
// рассылка с сильной гарантией, история, индекс по времени — остаётся в
// BasicChatStore и работает с данными только через этот интерфейс. Поэтому
// одни и те же тесты проходят на каждой реализации, и выбор реализации —
// параметр запуска (--engine, CHAT_ENGINE).
//
// Room — параметр шаблона: комната хранит историю элементов типа M, а
// тесты подставляют вместо сообщения инструментированные типы.
//
// Интерфейс виртуальный, а не шаблонный параметр хранилища: движок
// выбирается при запуске, а не при компиляции. Цена — косвенный вызов на
// каждый поиск; она измерена в docs/benchmarks.md.
template <class Room>
class IChatStore {
public:
    using UserPtr = std::shared_ptr<User>;

    virtual ~IChatStore() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // --- Комнаты -----------------------------------------------------------
    [[nodiscard]] virtual Room* find_room(std::string_view room) noexcept = 0;
    [[nodiscard]] virtual const Room* find_room(std::string_view room) const noexcept = 0;
    // Новая комната; предусловие — такой ещё нет. Сильная гарантия.
    virtual Room& add_room(std::string name, Room room) = 0;
    [[nodiscard]] virtual std::size_t room_count() const noexcept = 0;
    // Обход в порядке, который выбирает таблица. Упорядоченный по имени
    // обход — дело упорядоченного индекса в хранилище.
    virtual void for_each_room(const std::function<void(const std::string&, Room&)>& fn) = 0;

    // --- Реестр ников ------------------------------------------------------
    [[nodiscard]] virtual const UserPtr* find_user(std::string_view nick) const noexcept = 0;
    // Новый ник; предусловие — такого ещё нет. Сильная гарантия.
    virtual void add_user(std::string nick, UserPtr user) = 0;
    virtual bool remove_user(std::string_view nick) noexcept = 0;
    // Перенос записи под новый ник; предусловия — from есть, to нет.
    // Сильная гарантия: при броске реестр прежний.
    virtual void move_user(std::string_view from, std::string to) = 0;
    [[nodiscard]] virtual std::size_t user_count() const noexcept = 0;

protected:
    IChatStore() = default;
    IChatStore(const IChatStore&) = default;
    IChatStore& operator=(const IChatStore&) = default;
};


// Фабрика — объявлена здесь, определена в engines.hpp. Хранилище зовёт её в
// удобных конструкторах, не видя ни одной реализации; для комнаты ChatStore
// она инстанцирована в infrastructure/src/engines.cpp, для тестовых типов — в тестах,
// которые включают engines.hpp.
template <class Room>
[[nodiscard]] std::unique_ptr<IChatStore<Room>> make_chat_store(EngineKind kind);

}  // namespace chat
