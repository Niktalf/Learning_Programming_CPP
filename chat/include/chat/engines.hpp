#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "chat/chained_table.hpp"
#include "chat/chat_data.hpp"
#include "chat/hash.hpp"
#include "chat/open_table.hpp"

namespace chat {

// Этап 9: реализации данных чата. Этот заголовок включают точка сборки
// (src/engines.cpp), тесты движков и таблиц и замеры. Хранилище его не
// включает.

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
