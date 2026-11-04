// Участники комнаты на weak_ptr: реестр владеет, комната наблюдает.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "chat/chat_store.hpp"

using chat::ChatStore;

TEST(Members, RoomDoesNotKeepDepartedUserAlive) {
    ChatStore store;
    std::weak_ptr<chat::User> observer = store.register_user("bob");
    store.join("#general", "bob");

    EXPECT_FALSE(observer.expired());
    store.release_user("bob");

    // Реестр отпустил пользователя — объект уничтожен, хотя запись в составе
    // комнаты осталась: комната только наблюдала.
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(observer.lock(), nullptr);
    EXPECT_FALSE(store.is_member("#general", "bob"));
    EXPECT_TRUE(store.members("#general")->empty());
}

TEST(Members, PurgeRemovesExpiredEntries) {
    ChatStore store;
    store.register_user("alice");
    store.register_user("bob");
    store.join("#a", "alice");
    store.join("#a", "bob");
    store.join("#b", "bob");

    store.release_user("bob");
    // Без очистки записи копятся на каждого ушедшего.
    EXPECT_EQ(store.member_entries("#a"), 2u);
    EXPECT_EQ(store.member_entries("#b"), 1u);

    EXPECT_EQ(store.purge_departed(), 2u);
    EXPECT_EQ(store.member_entries("#a"), 1u);
    EXPECT_EQ(store.member_entries("#b"), 0u);
}

TEST(Members, JoinCleansItsRoom) {
    ChatStore store;
    store.register_user("bob");
    store.join("#a", "bob");
    store.release_user("bob");

    store.register_user("carol");
    store.join("#a", "carol");
    // Вход в комнату заодно убрал из неё истёкшую запись.
    EXPECT_EQ(store.member_entries("#a"), 1u);
}

TEST(Members, LockedMembersSurviveDeparture) {
    ChatStore store;
    store.register_user("bob");
    store.join("#general", "bob");

    // На время рассылки участники удерживаются: результат members держит
    // shared_ptr. Пользователь ушёл из реестра посреди операции — объект
    // всё ещё жив, и обращение к нему корректно.
    const auto recipients = store.members("#general");
    store.release_user("bob");
    ASSERT_EQ(recipients->size(), 1u);
    EXPECT_EQ(recipients->front()->nick(), std::string("bob"));
    EXPECT_EQ(recipients->front().use_count(), 1);   // держит только recipients
}

TEST(Members, SameNickAfterReturnIsADifferentUser) {
    ChatStore store;
    store.register_user("bob");
    store.join("#general", "bob");
    store.release_user("bob");

    // Вернулся пользователь с тем же ником — это другой объект. Комната
    // не путает его с прежним: owner_less сравнивает контрольные блоки,
    // а не строки.
    store.register_user("bob");
    EXPECT_FALSE(store.is_member("#general", "bob"));
}
