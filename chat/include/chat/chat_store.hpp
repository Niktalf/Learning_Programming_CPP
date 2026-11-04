#pragma once

#include <cstddef>
#include <map>
#include <string>
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

    bool create_room(std::string room) {
        // try_emplace появится в C++17; пока обходимся поиском и вставкой.
        if (rooms_.find(room) != rooms_.end()) {
            return false;
        }
        rooms_.insert(std::make_pair(std::move(room), History{}));
        return true;
    }

    // Комната создаётся при первой отправке — это осознанное решение, а не
    // недосмотр: в чате войти в несуществующую комнату значит её завести.
    // Обратите внимание, что для чтения такого поведения быть не должно.
    void post(std::string room, M message) {
        typename Map::iterator it = rooms_.find(room);
        if (it == rooms_.end()) {
            it = rooms_.insert(std::make_pair(std::move(room), History{})).first;
        }
        it->second.push_back(std::move(message));
    }

    // Возврат сырого указателя на этом этапе намеренный: nullptr означает
    // «комнаты нет». Интерфейс неудобен — вызывающий обязан помнить о
    // проверке, а тип ничего об этом не сообщает. На этапе 2 он заменится
    // на std::optional, и разницу стоит увидеть своими глазами.
    const History* history(const std::string& room) const noexcept {
        typename Map::const_iterator it = rooms_.find(room);
        return it == rooms_.end() ? nullptr : &it->second;
    }

    bool drop_room(const std::string& room) {
        typename Map::iterator it = rooms_.find(room);
        if (it == rooms_.end()) {
            return false;
        }
        rooms_.erase(it);
        return true;
    }

    std::size_t room_count() const noexcept { return rooms_.size(); }
    bool empty() const noexcept { return rooms_.empty(); }

    // Функтор принимается по значению — как это делают алгоритмы стандартной
    // библиотеки. Именно поэтому лямбда с захватом по значению и mutable не
    // выносит изменения наружу: меняется копия.
    //
    // Разложение пары на имена появится в C++17, пока обращаемся к first
    // и second.
    template <class F>
    void for_each_room(F fn) const {
        for (const auto& entry : rooms_) {
            fn(entry.first, entry.second);
        }
    }

private:
    using Map = std::map<std::string, History>;
    Map rooms_;
};

using ChatStore = BasicChatStore<Message>;

}  // namespace chat
