#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "chat/chained_table.hpp"
#include "chat/hash.hpp"
#include "chat/open_table.hpp"
#include "chat/user.hpp"

namespace chat {

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

// Первая реализация — код этапа 7, переехавший за интерфейс: обе таблицы на
// std::unordered_map со стандартным хешем. На этапе 7 комнаты лежали в
// std::map ради алфавитного /rooms; теперь алфавитный порядок держит
// отдельный упорядоченный индекс имён в хранилище, и для таблицы комнат
// остаётся только поиск — работа для хеш-таблицы.
template <class Room>
class StdChatStore final : public IChatStore<Room> {
    using UserPtr = typename IChatStore<Room>::UserPtr;

    struct NameHash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view name) const noexcept {
            return std::hash<std::string_view>{}(name);
        }
    };
    template <class V>
    using Map = std::unordered_map<std::string, V, NameHash, std::equal_to<>>;

public:
    [[nodiscard]] std::string_view name() const noexcept override { return "std"; }

    [[nodiscard]] Room* find_room(std::string_view room) noexcept override {
        const auto it = rooms_.find(room);
        return it == rooms_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const Room* find_room(std::string_view room) const noexcept override {
        const auto it = rooms_.find(room);
        return it == rooms_.end() ? nullptr : &it->second;
    }
    Room& add_room(std::string name, Room room) override {
        return rooms_.try_emplace(std::move(name), std::move(room)).first->second;
    }
    [[nodiscard]] std::size_t room_count() const noexcept override { return rooms_.size(); }
    void for_each_room(const std::function<void(const std::string&, Room&)>& fn) override {
        std::ranges::for_each(rooms_, [&fn](auto& pair) { fn(pair.first, pair.second); });
    }

    [[nodiscard]] const UserPtr* find_user(std::string_view nick) const noexcept override {
        const auto it = users_.find(nick);
        return it == users_.end() ? nullptr : &it->second;
    }
    void add_user(std::string nick, UserPtr user) override { users_.emplace(std::move(nick), std::move(user)); }
    bool remove_user(std::string_view nick) noexcept override {
        const auto it = users_.find(nick);
        if (it == users_.end()) {
            return false;
        }
        users_.erase(it);
        return true;
    }
    // Перенос узла (extract/insert из C++17) не выделяет памяти: узел
    // только что извлечён из этой же таблицы, и после вставки элементов
    // столько же, сколько было, — массиву корзин расти незачем.
    void move_user(std::string_view from, std::string to) override {
        auto node = users_.extract(users_.find(from));
        node.key() = std::move(to);
        users_.insert(std::move(node));
    }
    [[nodiscard]] std::size_t user_count() const noexcept override { return users_.size(); }

private:
    Map<Room> rooms_;
    Map<UserPtr> users_;
};

// Реализации на собственных таблицах: Table — шаблон таблицы
// (ChainedTable или OpenTable), Hash — хеш-функция.
template <class Room, template <class, class, class, class> class Table, class Hash>
class TableChatStore final : public IChatStore<Room> {
    using UserPtr = typename IChatStore<Room>::UserPtr;

public:
    TableChatStore(std::string_view name, Hash hash) : name_(name), rooms_(hash), users_(hash) {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }

    [[nodiscard]] Room* find_room(std::string_view room) noexcept override { return rooms_.find(room); }
    [[nodiscard]] const Room* find_room(std::string_view room) const noexcept override {
        return rooms_.find(room);
    }
    Room& add_room(std::string name, Room room) override {
        return rooms_.insert_new(std::move(name), std::move(room));
    }
    [[nodiscard]] std::size_t room_count() const noexcept override { return rooms_.size(); }
    void for_each_room(const std::function<void(const std::string&, Room&)>& fn) override {
        rooms_.for_each(fn);
    }

    [[nodiscard]] const UserPtr* find_user(std::string_view nick) const noexcept override {
        return users_.find(nick);
    }
    void add_user(std::string nick, UserPtr user) override {
        users_.insert_new(std::move(nick), std::move(user));
    }
    bool remove_user(std::string_view nick) noexcept override { return users_.erase(nick); }
    // Извлечения узла у собственных таблиц нет, поэтому перенос — вставка
    // под новым ником и затем удаление старого. Вставка может бросить, но
    // до неё ничего не изменено; удаление не бросает. Копия shared_ptr —
    // только счётчик ссылок, она не бросает.
    void move_user(std::string_view from, std::string to) override {
        UserPtr user = *users_.find(from);
        users_.insert_new(std::move(to), std::move(user));
        users_.erase(from);
    }
    [[nodiscard]] std::size_t user_count() const noexcept override { return users_.size(); }

private:
    std::string_view name_;   // строковый литерал из make_chat_store
    Table<std::string, Room, Hash, std::equal_to<>> rooms_;
    Table<std::string, UserPtr, Hash, std::equal_to<>> users_;
};

enum class EngineKind { Std, Hash, BadHash, Open };

inline constexpr std::array<EngineKind, 4> all_engine_kinds = {EngineKind::Std, EngineKind::Hash,
                                                               EngineKind::BadHash, EngineKind::Open};

[[nodiscard]] constexpr std::string_view name_of(EngineKind kind) noexcept {
    switch (kind) {
        case EngineKind::Std:
            return "std";
        case EngineKind::Hash:
            return "hash";
        case EngineKind::BadHash:
            return "bad-hash";
        case EngineKind::Open:
            return "open";
    }
    return "std";
}

// Поиск по проекции: find сравнивает с name не сам элемент, а name_of от
// него.
[[nodiscard]] inline std::optional<EngineKind> parse_engine_kind(std::string_view name) noexcept {
    const auto found = std::ranges::find(all_engine_kinds, name, name_of);
    return found == all_engine_kinds.end() ? std::nullopt : std::optional<EngineKind>(*found);
}

// Движок по умолчанию — из переменной окружения CHAT_ENGINE, иначе std. Так
// весь набор тестов прогоняется на каждом движке без единой правки: ctest
// запускает его четыре раза с разным CHAT_ENGINE (CMakeLists.txt).
// Неизвестное имя — std: консольный клиент проверяет --engine сам и
// отвергает опечатку с сообщением.
[[nodiscard]] inline EngineKind default_engine_kind() noexcept {
    const char* const value = std::getenv("CHAT_ENGINE");   // NOLINT: окружение читается до потоков
    return value == nullptr ? EngineKind::Std : parse_engine_kind(value).value_or(EngineKind::Std);
}

// Случайная затравка хеша на процесс: ники выбирают клиенты, и подобрать
// ники с одинаковым хешем, не зная затравки, труднее (docs/hashing.md).
[[nodiscard]] inline std::uint64_t random_seed() {
    static const std::uint64_t seed = [] {
        std::random_device device;
        return (static_cast<std::uint64_t>(device()) << 32) ^ device();
    }();
    return seed;
}

template <class Room>
[[nodiscard]] std::unique_ptr<IChatStore<Room>> make_chat_store(EngineKind kind) {
    switch (kind) {
        case EngineKind::Hash:
            return std::make_unique<TableChatStore<Room, ChainedTable, Fnv1aHash>>("hash",
                                                                                    Fnv1aHash{random_seed()});
        case EngineKind::BadHash:
            return std::make_unique<TableChatStore<Room, ChainedTable, CharSumHash>>("bad-hash", CharSumHash{});
        case EngineKind::Open:
            return std::make_unique<TableChatStore<Room, OpenTable, Fnv1aHash>>("open",
                                                                                 Fnv1aHash{random_seed()});
        case EngineKind::Std:
            break;
    }
    return std::make_unique<StdChatStore<Room>>();
}

}  // namespace chat
