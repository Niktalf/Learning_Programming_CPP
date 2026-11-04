#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "chat/chat_store.hpp"
#include "tracked.hpp"

using chat::BasicChatStore;
using chat::ChatStore;
using chat::make_text;
using chat::testing::Tracked;

TEST(ChatStore, PostThenReadHistory) {
    ChatStore store;
    store.create_room("#general");
    store.post("#general", make_text("alice", "hello"));
    store.post("#general", make_text("bob", "hi"));

    // if с инициализацией: результат ограничен областью проверки.
    if (const auto history = store.history("#general"); history) {
        const auto& messages = history->get();
        ASSERT_EQ(messages.size(), 2u);
        EXPECT_EQ(messages[0]->author(), std::string("alice"));
        EXPECT_EQ(messages[1]->to_string(), std::string("hi"));
    } else {
        ADD_FAILURE() << "room must exist";
    }
}

TEST(ChatStore, MissingRoomReadsAsEmptyAndCreatesNothing) {
    ChatStore store;
    EXPECT_FALSE(store.history("#absent").has_value());
    // Чтение истории несуществующей комнаты не должно её создавать.
    // С operator[] на map здесь появилась бы пустая комната.
    EXPECT_EQ(store.room_count(), 0u);
    EXPECT_TRUE(store.empty());
}

TEST(ChatStore, CreateRoomReportsWhetherItWasNew) {
    ChatStore store;
    EXPECT_TRUE(store.create_room("#general"));
    EXPECT_FALSE(store.create_room("#general"));
    EXPECT_EQ(store.room_count(), 1u);
}

TEST(ChatStore, JoinAndLeaveTrackMembers) {
    ChatStore store;
    store.register_user("alice");
    store.register_user("bob");
    EXPECT_TRUE(store.join("#general", "alice"));
    EXPECT_FALSE(store.join("#general", "alice"));   // повторный вход ничего не меняет
    EXPECT_TRUE(store.join("#general", "bob"));
    EXPECT_TRUE(store.is_member("#general", "bob"));

    EXPECT_TRUE(store.leave("#general", "bob"));
    EXPECT_FALSE(store.leave("#general", "bob"));
    EXPECT_FALSE(store.leave("#absent", "bob"));

    const auto members = store.members("#general");
    ASSERT_TRUE(members.has_value());
    ASSERT_EQ(members->size(), 1u);
    EXPECT_EQ(members->front()->nick(), std::string("alice"));
}

TEST(ChatStore, MessageAtGivesOptionalReferenceInsteadOfPointer) {
    ChatStore store;
    store.create_room("#general");
    store.post("#general", make_text("alice", "hello"));

    EXPECT_TRUE(store.message_at("#general", 0).has_value());
    EXPECT_FALSE(store.message_at("#general", 1).has_value());
    EXPECT_FALSE(store.message_at("#absent", 0).has_value());
}

TEST(ChatStore, TopicBelongsToExistingRoom) {
    ChatStore store;
    EXPECT_FALSE(store.set_topic("#general", "x"));
    store.create_room("#general");
    EXPECT_TRUE(store.set_topic("#general", "курс по C++"));
    EXPECT_EQ(store.topic("#general").value_or(""), std::string("курс по C++"));
}

TEST(ChatStore, PostingTemporaryDoesNotCopy) {
    BasicChatStore<Tracked> store;
    store.create_room("#general");
    Tracked::reset();

    store.post("#general", Tracked{std::string("payload")});

    EXPECT_EQ(Tracked::copies(), 0);
    EXPECT_GE(Tracked::moves(), 1);
}

TEST(ChatStore, PostingNamedMessageCopiesExactlyOnce) {
    BasicChatStore<Tracked> store;
    store.create_room("#general");
    Tracked named{std::string("payload")};
    Tracked::reset();

    store.post("#general", named);   // копия в параметр, дальше перемещение

    EXPECT_EQ(Tracked::copies(), 1);
}

TEST(ChatStore, ExplicitMoveDoesNotCopy) {
    BasicChatStore<Tracked> store;
    store.create_room("#general");
    Tracked named{std::string("payload")};
    Tracked::reset();

    store.post("#general", std::move(named));

    EXPECT_EQ(Tracked::copies(), 0);
}

TEST(ChatStore, ForEachRoomVisitsRoomsInNameOrder) {
    ChatStore store;
    store.create_room("#b");
    store.create_room("#a");
    store.create_room("#c");
    store.post("#b", make_text("u", "2"));
    store.post("#a", make_text("u", "1"));
    store.post("#c", make_text("u", "3"));

    std::vector<std::string> seen;
    store.for_each_room([&seen](const std::string& room, const chat::History&) {
        seen.push_back(room);
    });

    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0], std::string("#a"));
    EXPECT_EQ(seen[1], std::string("#b"));
    EXPECT_EQ(seen[2], std::string("#c"));
}

TEST(ChatStore, CaptureByReferenceAccumulatesButByValueDoesNot) {
    ChatStore store;
    store.create_room("#a");
    store.create_room("#b");
    store.post("#a", make_text("u", "1"));
    store.post("#a", make_text("u", "2"));
    store.post("#b", make_text("u", "3"));

    int by_reference = 0;
    store.for_each_room([&by_reference](const std::string&, const chat::History& h) {
        by_reference += static_cast<int>(h.size());
    });

    int by_value = 0;
    store.for_each_room([by_value](const std::string&, const chat::History& h) mutable {
        by_value += static_cast<int>(h.size());
    });

    EXPECT_EQ(by_reference, 3);   // 2 + 1
    EXPECT_EQ(by_value, 0);       // менялась копия, наружу ничего не вышло
}

TEST(ChatStore, PostToUnknownRoomThrows) {
    ChatStore store;
    // Этап 3: комнаты создаёт только вход в них, отправка — нет.
    EXPECT_THROW(store.post("#absent", make_text("alice", "hello")), chat::UnknownRoomError);
    EXPECT_EQ(store.room_count(), 0u);
}

TEST(ChatStore, NicknameRegistryRejectsDuplicates) {
    ChatStore store;
    store.register_user("alice");
    EXPECT_THROW(store.register_user("alice"), chat::NicknameTakenError);
    store.release_user("alice");
    EXPECT_NO_THROW(store.register_user("alice"));
}

TEST(ChatStore, JoinNeedsRegisteredUser) {
    ChatStore store;
    EXPECT_THROW(store.join("#general", "ghost"), chat::UnknownUserError);
    EXPECT_EQ(store.room_count(), 0u);   // комната не создана
}

TEST(ChatStore, RenameTouchesOneObjectAndNoRoom) {
    ChatStore store;
    const auto alice = store.register_user("alice");
    store.join("#a", "alice");
    store.join("#b", "alice");
    store.register_user("bob");
    store.join("#c", "bob");

    store.rename("alice", "alicia");

    // Комнаты хранят ссылку на объект, а не строку: тот же объект, новое имя.
    EXPECT_EQ(alice->nick(), std::string("alicia"));
    EXPECT_EQ(store.find_user("alicia"), alice);
    EXPECT_FALSE(store.has_user("alice"));
    EXPECT_TRUE(store.is_member("#a", "alicia"));
    EXPECT_TRUE(store.is_member("#b", "alicia"));
    EXPECT_FALSE(store.is_member("#c", "alicia"));
}

TEST(ChatStore, RenameToTakenNickChangesNothing) {
    ChatStore store;
    store.register_user("alice");
    store.register_user("bob");
    store.join("#a", "alice");

    EXPECT_THROW(store.rename("alice", "bob"), chat::NicknameTakenError);
    EXPECT_TRUE(store.has_user("alice"));
    EXPECT_EQ(store.find_user("alice")->nick(), std::string("alice"));
    EXPECT_TRUE(store.is_member("#a", "alice"));
}
