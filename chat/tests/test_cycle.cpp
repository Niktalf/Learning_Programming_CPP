// Звёздочка этапа 4: цикл из shared_ptr. Утечку здесь видно по счётчику
// живых объектов, без санитайзера. Чтобы сам тест не утекал, цикл в конце
// разрывается руками.

#include <gtest/gtest.h>

#include <memory>

#include "cycle.hpp"

using chat::cycle::alive;
using chat::cycle::join;

TEST(Cycle, SharedMembersKeepBothAlive) {
    using User = chat::cycle::User<std::shared_ptr>;
    using Room = chat::cycle::Room<std::shared_ptr>;
    std::weak_ptr<User> user_observer;
    std::weak_ptr<Room> room_observer;
    {
        const auto alice = std::make_shared<User>("alice");
        const auto general = std::make_shared<Room>("#general");
        join(alice, general);
        user_observer = alice;
        room_observer = general;
    }
    // Внешних ссылок нет, а оба живы: держат друг друга.
    EXPECT_FALSE(user_observer.expired());
    EXPECT_FALSE(room_observer.expired());
    EXPECT_EQ(alive(), 2);

    room_observer.lock()->members.clear();   // разрыв руками
    EXPECT_TRUE(user_observer.expired());
    EXPECT_TRUE(room_observer.expired());
    EXPECT_EQ(alive(), 0);
}

TEST(Cycle, WeakMembersBreakTheCycle) {
    using User = chat::cycle::User<std::weak_ptr>;
    using Room = chat::cycle::Room<std::weak_ptr>;
    std::weak_ptr<User> user_observer;
    std::weak_ptr<Room> room_observer;
    {
        const auto alice = std::make_shared<User>("alice");
        const auto general = std::make_shared<Room>("#general");
        join(alice, general);
        user_observer = alice;
        room_observer = general;
        EXPECT_EQ(alice.use_count(), 1);   // комната не владеет
        EXPECT_EQ(chat::cycle::alive_members(*general), 1);
    }
    EXPECT_TRUE(user_observer.expired());
    EXPECT_TRUE(room_observer.expired());
    EXPECT_EQ(alive(), 0);
}

TEST(Cycle, RoomOutlivingMemberSeesItGone) {
    using User = chat::cycle::User<std::weak_ptr>;
    using Room = chat::cycle::Room<std::weak_ptr>;
    const auto general = std::make_shared<Room>("#general");
    {
        const auto alice = std::make_shared<User>("alice");
        join(alice, general);
        EXPECT_EQ(chat::cycle::alive_members(*general), 1);
    }
    // Пользователь ушёл — комната его не удержала и это видит.
    EXPECT_EQ(chat::cycle::alive_members(*general), 0);
    EXPECT_EQ(alive(), 1);
}
