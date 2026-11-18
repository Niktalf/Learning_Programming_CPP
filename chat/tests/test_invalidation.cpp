// Домашнее задание этапа 7, исправление: удаление внутри обхода через
// it = c.erase(it). Ошибочная версия — tools/invalidation_demo.cpp.

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/user.hpp"

namespace {

using Members = std::set<std::weak_ptr<chat::User>, std::owner_less<>>;

struct Room {
    std::vector<std::shared_ptr<chat::User>> users;
    Members members;

    Room() {
        for (int i = 0; i < 16; ++i) {
            users.push_back(std::make_shared<chat::User>("user" + std::to_string(i)));
            members.insert(users.back());
        }
        for (std::size_t i = 0; i < users.size(); i += 2) {
            users[i].reset();   // чётные ушли
        }
    }
};

}  // namespace

TEST(Invalidation, EraseReturnsTheNextValidIterator) {
    Room room;
    for (auto it = room.members.begin(); it != room.members.end();) {
        if (it->expired()) {
            it = room.members.erase(it);
        } else {
            ++it;
        }
    }
    EXPECT_EQ(room.members.size(), 8u);
}

TEST(Invalidation, EraseIfIsTheShortWay) {
    Room room;
    EXPECT_EQ(std::erase_if(room.members, [](const auto& member) { return member.expired(); }), 8u);
    EXPECT_EQ(room.members.size(), 8u);
}

TEST(Invalidation, StorePurgesDepartedMembers) {
    // BasicChatStore::purge с этапа 7 — тот же std::erase_if.
    chat::ChatStore store;
    store.register_user("alice");
    store.register_user("bob");
    store.join("#general", "alice");
    store.join("#general", "bob");
    store.release_user("bob");
    EXPECT_EQ(store.member_entries("#general"), 2u);   // запись bob истекла, но осталась
    EXPECT_EQ(store.purge_departed(), 1u);
    EXPECT_EQ(store.member_entries("#general"), 1u);
}

TEST(Invalidation, SetEraseInvalidatesOnlyTheErasedNode) {
    // Правило std::set (и std::map): erase делает недействительным только
    // итератор на удалённый узел. Остальные действительны — поэтому
    // it = erase(it) вообще работает.
    Room room;
    auto kept = room.members.begin();
    while (kept->expired()) {
        ++kept;
    }
    const std::shared_ptr<chat::User> kept_user = kept->lock();
    std::erase_if(room.members, [](const auto& member) { return member.expired(); });
    EXPECT_EQ(kept->lock(), kept_user);
}
