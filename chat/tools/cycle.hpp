#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

// Звёздочка этапа 4: цикл из shared_ptr и его разрыв.
//
// Пользователь держит комнаты, в которые вошёл, — «мои комнаты». Комната
// держит своих участников — чтобы знать, кому рассылать. Если обе ссылки
// владеющие, пользователь держит комнату, комната держит пользователя, и
// счётчики никогда не дойдут до нуля: оба объекта переживут последнюю внешнюю
// ссылку и не освободятся до конца программы. Санитайзер покажет утечку.
//
// Вид ссылки от комнаты к участникам — параметр шаблона: std::shared_ptr или
// std::weak_ptr. Настоящее хранилище устроено именно как второй вариант:
// комната хранит weak_ptr на пользователей, владеет ими реестр.
//
// Правило: обратная ссылка почти всегда должна быть weak. Здесь «обратная» —
// от комнаты к участнику: участник может уйти, и комната не должна его
// удерживать.
namespace chat::cycle {

// Сколько объектов обоих видов сейчас живо.
inline int& alive() {
    static int count = 0;
    return count;
}

template <template <class> class Link>
struct User;

template <template <class> class Link>
struct Room {
    explicit Room(std::string room_name) : name(std::move(room_name)) { ++alive(); }
    Room(const Room&) = delete;
    Room& operator=(const Room&) = delete;
    ~Room() { --alive(); }

    std::string name;
    std::vector<Link<User<Link>>> members;   // shared_ptr или weak_ptr
};

template <template <class> class Link>
struct User {
    explicit User(std::string user_nick) : nick(std::move(user_nick)) { ++alive(); }
    User(const User&) = delete;
    User& operator=(const User&) = delete;
    ~User() { --alive(); }

    std::string nick;
    std::vector<std::shared_ptr<Room<Link>>> rooms;   // владеющие ссылки
};

template <template <class> class Link>
void join(const std::shared_ptr<User<Link>>& user, const std::shared_ptr<Room<Link>>& room) {
    user->rooms.push_back(room);
    room->members.push_back(user);
}

// Сколько участников комнаты ещё живы. С weak_ptr каждого надо захватить
// через lock: ушедший пользователь уже не участник.
inline int alive_members(const Room<std::weak_ptr>& room) {
    int count = 0;
    for (const auto& member : room.members) {
        count += member.expired() ? 0 : 1;
    }
    return count;
}

inline int alive_members(const Room<std::shared_ptr>& room) {
    return static_cast<int>(room.members.size());
}

}  // namespace chat::cycle
