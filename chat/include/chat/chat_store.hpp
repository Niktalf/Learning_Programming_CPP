#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "chat/errors.hpp"
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

    // Добавление сообщения. Этап 3: комнаты создаёт только /join, и отправка
    // в несуществующую комнату — UnknownRoomError, а не молчаливое создание.
    // Проверка «писать может только участник» — правило команды, а не
    // хранилища, и живёт в исполнителе.
    void post(std::string_view room, M message) {
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            throw UnknownRoomError(std::string(room));
        }
        it->second.history.push_back(std::move(message));
    }

    // Рассылка в несколько комнат с сильной гарантией: сообщение появляется
    // либо во всех комнатах, либо ни в одной.
    //
    // Всё, что может бросить, делается в фазе подготовки, и изменяет только
    // то, что не видно снаружи: поиск комнат, копии сообщения, запас ёмкости
    // в историях. Фаза фиксации бросить не может — это проверяет компилятор.
    //
    // Полная копия всех комнат с последующим swap дала бы ту же гарантию
    // проще, но ценой копирования всей истории чата на каждое сообщение.
    // Здесь копируется только само сообщение — по разу на комнату.
    // Сравнение подходов — в docs/errors.md.
    void post_many(const std::vector<std::string_view>& rooms, M message) {
        // Фиксация переносит копии в истории перемещением. Бросающее
        // перемещение сделало бы точку невозврата точкой частичной записи:
        // первые комнаты получили бы сообщение, остальные нет.
        static_assert(std::is_nothrow_move_constructible_v<M>,
                      "post_many needs a message type with a noexcept move constructor");
        if (rooms.empty()) {
            return;
        }

        // Подготовка, шаг 1: все комнаты существуют.
        std::vector<History*> targets;
        targets.reserve(rooms.size());
        for (const std::string_view room : rooms) {
            const auto it = rooms_.find(room);
            if (it == rooms_.end()) {
                throw UnknownRoomError(std::string(room));
            }
            History* const history = &it->second.history;
            // Повтор разбор отсекает раньше; здесь он нарушил бы расчёт
            // ёмкости ниже — запас на одно сообщение, а пришло бы два.
            if (std::find(targets.begin(), targets.end(), history) != targets.end()) {
                throw ChatError("room listed twice: " + std::string(room));
            }
            targets.push_back(history);
        }

        // Шаг 2: копии сообщения — по одной на каждую комнату, кроме
        // последней: ей достаётся оригинал. Здесь бросают копирования.
        std::vector<M> copies;
        copies.reserve(targets.size());
        for (std::size_t i = 0; i + 1 < targets.size(); ++i) {
            copies.push_back(message);
        }
        copies.push_back(std::move(message));

        // Шаг 3: запас ёмкости, чтобы push_back при фиксации не выделял
        // память. Ёмкость — не наблюдаемое состояние: история с запасом
        // содержит те же сообщения, поэтому бросок здесь гарантию не портит.
        for (History* const history : targets) {
            reserve_one_more(*history);
        }

        commit(targets, copies);
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

    // Реестр ников. Ник занимается при входе в чат и освобождается при
    // выходе; занятый ник — NicknameTakenError.
    void register_nick(std::string nick) {
        if (nicks_.count(nick) != 0) {
            throw NicknameTakenError(std::move(nick));
        }
        nicks_.insert(std::move(nick));
    }

    void release_nick(std::string_view nick) {
        if (const auto it = nicks_.find(nick); it != nicks_.end()) {
            nicks_.erase(it);
        }
    }

    [[nodiscard]] bool has_nick(std::string_view nick) const {
        return nicks_.find(nick) != nicks_.end();
    }

    // Смена ника — ещё одна операция над несколькими комнатами: ник надо
    // заменить в реестре и в составе каждой комнаты, где участник состоит.
    // Гарантия та же, что у post_many: новые составы готовятся в стороне,
    // затем меняются местами с текущими. swap у std::set — noexcept, поэтому
    // фиксация не бросает.
    //
    // Уже отправленные сообщения сохраняют старое имя автора: история — это
    // запись того, что было.
    void rename(std::string_view from, std::string to) {
        if (from == to) {
            return;
        }
        if (nicks_.count(to) != 0) {
            throw NicknameTakenError(std::move(to));
        }

        Members next_nicks = nicks_;
        next_nicks.erase(std::string(from));
        next_nicks.insert(to);

        std::vector<std::pair<Members*, Members>> next_members;
        for (auto& [name, room] : rooms_) {
            if (room.members.find(from) == room.members.end()) {
                continue;
            }
            Members updated = room.members;
            updated.erase(std::string(from));
            updated.insert(to);
            next_members.emplace_back(&room.members, std::move(updated));
        }

        // Точка невозврата: дальше только обмены, ни один не бросает.
        nicks_.swap(next_nicks);
        for (auto& [target, updated] : next_members) {
            target->swap(updated);
        }
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
    static void reserve_one_more(History& history) {
        if (history.size() == history.capacity()) {
            // Удвоение, а не +1: иначе каждая рассылка перевыделяла бы
            // историю, и добавление стало бы линейным вместо амортизированно
            // постоянного.
            history.reserve(history.empty() ? 8 : history.size() * 2);
        }
    }

    // Фиксация рассылки. noexcept здесь — контракт: ёмкость запасена,
    // перемещение не бросает, значит не бросает и push_back. Если контракт
    // всё же нарушен, программа завершится через std::terminate. Это лучше,
    // чем чат, в котором часть участников видит сообщение, а часть нет.
    static void commit(const std::vector<History*>& targets, std::vector<M>& copies) noexcept {
        for (std::size_t i = 0; i < targets.size(); ++i) {
            targets[i]->push_back(std::move(copies[i]));
        }
    }

    // std::less<> включает разнородный поиск: методы принимают string_view
    // и не создают временную строку на каждый вызов.
    using Map = std::map<std::string, Room, std::less<>>;
    Map rooms_;
    Members nicks_;
};

using ChatStore = BasicChatStore<Message>;
using History = ChatStore::History;

}  // namespace chat
