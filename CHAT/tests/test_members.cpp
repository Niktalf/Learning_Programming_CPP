#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "chat_store.hpp"
#include "errors.hpp"

TEST(Members, InvitedUserIsVisible) {
    ChatStore store;
    store.create_room("#general");
    store.claim_nick("alice");
    store.invite("#general", "alice");

    const auto alive = store.members("#general");
    ASSERT_EQ(alive.size(), 1u);
    EXPECT_EQ(alive[0]->nick(), std::string("alice"));
}

TEST(Members, RoomDoesNotKeepLoggedOutUserAlive) {
    ChatStore store;
    store.create_room("#general");
    store.claim_nick("alice");
    store.invite("#general", "alice");

    std::weak_ptr<User> observer;
    {
        const auto alive = store.members("#general");
        observer = alive[0];
    }

    EXPECT_FALSE(observer.expired());
    EXPECT_TRUE(store.release_nick("alice"));
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(observer.lock(), nullptr);
    EXPECT_TRUE(store.members("#general").empty());
}

TEST(Members, HeldResultKeepsUserAliveDuringUse) {
    ChatStore store;
    store.create_room("#general");
    store.claim_nick("alice");
    store.invite("#general", "alice");

    const auto alive = store.members("#general");
    store.release_nick("alice");
    ASSERT_EQ(alive.size(), 1u);
    EXPECT_EQ(alive[0]->nick(), std::string("alice"));   // объект ещё жив
}

TEST(Members, InviteRequiresExistingUserAndRoom) {
    ChatStore store;
    store.create_room("#general");
    EXPECT_THROW(store.invite("#general", "ghost"), UnknownUserError);
    store.claim_nick("alice");
    EXPECT_THROW(store.invite("#absent", "alice"), UnknownRoomError);
}

TEST(Members, NicknameCannotBeClaimedTwice) {
    ChatStore store;
    store.claim_nick("alice");
    EXPECT_THROW(store.claim_nick("alice"), NicknameTakenError);
    EXPECT_EQ(store.user_count(), 1u);
}
