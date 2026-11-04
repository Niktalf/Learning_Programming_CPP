#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "chat/message.hpp"

namespace chat {

// Хранилище параметризовано типом сообщения по одной практической причине:
// в тестах вместо Message подставляется инструментированный тип, считающий
// копирования и перемещения. Без этого утверждение «добавление временного
// сообщения не копирует» проверить нечем.
template <class M>
class BasicChatStore {
public:
    using History = std::vector<M>;
    using Members = std::set<std::string, std::less<>>;

    // Комната — история, участники и тема. Структура, а не класс: инвариантов,
    // которые надо охранять, у неё нет, их держит хранилище.
    struct Room {
        History history;
        Members members;
        std::string topic;
    };

    bool create_room(std::string room) {
        // try_emplace из C++17: значение конструируется только при вставке.
        return rooms_.try_emplace(std::move(room)).second;
    }

    // Вход в комнату. Несуществующая комната создаётся — в чате войти в
    // новую комнату и значит её завести. Возвращает false, если участник уже
    // был в комнате.
    bool join(std::string_view room, std::string_view nick) {
        const auto it = rooms_.try_emplace(std::string(room)).first;
        return it->second.members.emplace(nick).second;
    }

    // Выход. false — если комнаты нет или участника в ней не было.
    bool leave(std::string_view room, std::string_view nick) {
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            return false;
        }
        Members& members = it->second.members;
        const auto member = members.find(nick);
        if (member == members.end()) {
            return false;
        }
        members.erase(member);
        return true;
    }

    [[nodiscard]] bool is_member(std::string_view room, std::string_view nick) const {
        const auto it = rooms_.find(room);
        return it != rooms_.end() && it->second.members.count(nick) != 0;
    }

    // Добавление сообщения. Как и на этапе 1, комната создаётся при первой
    // отправке; проверка «писать может только участник» — правило команды,
    // а не хранилища, и живёт в исполнителе.
    void post(std::string room, M message) {
        rooms_[std::move(room)].history.push_back(std::move(message));
    }

    // Этап 2: вместо сырого указателя — std::optional. Отсутствие комнаты
    // теперь выражено в типе, и вызывающий не может забыть о проверке.
    //
    // Внутри optional лежит reference_wrapper, а не копия. Причина
    // практическая: std::optional<T&> стандартом не предусмотрен, а копировать
    // историю из тысяч сообщений на каждый запрос недопустимо. Это тот случай,
    // когда optional не бесплатен и приходится помнить, что ссылка живёт
    // ровно столько, сколько живёт комната.
    [[nodiscard]] std::optional<std::reference_wrapper<const History>>
    history(std::string_view room) const {
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            return std::nullopt;
        }
        return std::cref(it->second.history);
    }

    [[nodiscard]] std::optional<std::reference_wrapper<const Members>>
    members(std::string_view room) const {
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            return std::nullopt;
        }
        return std::cref(it->second.members);
    }

    // Изменяющий доступ к сообщению нужен команде REACT. Возвращается
    // optional со ссылкой, а не сырой указатель: «нет такого сообщения»
    // выражено в типе так же, как «нет такой комнаты» у history.
    [[nodiscard]] std::optional<std::reference_wrapper<M>>
    message_at(std::string_view room, std::size_t index) {
        const auto it = rooms_.find(room);
        if (it == rooms_.end() || index >= it->second.history.size()) {
            return std::nullopt;
        }
        return std::ref(it->second.history[index]);
    }

    [[nodiscard]] std::optional<std::reference_wrapper<const M>>
    message_at(std::string_view room, std::size_t index) const {
        const auto it = rooms_.find(room);
        if (it == rooms_.end() || index >= it->second.history.size()) {
            return std::nullopt;
        }
        return std::cref(it->second.history[index]);
    }

    [[nodiscard]] std::optional<std::string> topic(std::string_view room) const {
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            return std::nullopt;
        }
        return it->second.topic;
    }

    bool set_topic(std::string_view room, std::string topic) {
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            return false;
        }
        it->second.topic = std::move(topic);
        return true;
    }

    [[nodiscard]] bool contains(std::string_view room) const {
        return rooms_.find(room) != rooms_.end();
    }

    [[nodiscard]] std::size_t room_count() const noexcept { return rooms_.size(); }
    [[nodiscard]] bool empty() const noexcept { return rooms_.empty(); }

    // Функтор принимается по значению — как это делают алгоритмы стандартной
    // библиотеки. Именно поэтому лямбда с захватом по значению и mutable не
    // выносит изменения наружу: меняется копия.
    //
    // Structured bindings из C++17: пара раскладывается на имена, а не
    // читается через first и second.
    template <class F>
    void for_each_room(F fn) const {
        for (const auto& [name, room] : rooms_) {
            fn(name, room.history);
        }
    }

private:
    // std::less<> включает разнородный поиск: методы принимают string_view
    // и не создают временную строку на каждый вызов.
    using Map = std::map<std::string, Room, std::less<>>;
    Map rooms_;
};

using ChatStore = BasicChatStore<Message>;
using History = ChatStore::History;

}  // namespace chat
