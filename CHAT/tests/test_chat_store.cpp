#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "chat_store.hpp"
#include "tracked.hpp"

TEST(ChatStore, PostThenReadHistory) {
    ChatStore store;
    store.post("#general", Message("alice", "hello"));
    store.post("#general", Message("bob", "hi"));

    const ChatStore::History* history = store.history("#general");
    ASSERT_NE(history, nullptr);
    ASSERT_EQ(history->size(), 2u);
    EXPECT_EQ((*history)[0].author(), std::string("alice"));
    EXPECT_EQ((*history)[1].text(), std::string("hi"));
}

TEST(ChatStore, MissingRoomReadsAsNullptrAndCreatesNothing) {
    ChatStore store;
    EXPECT_EQ(store.history("#absent"), nullptr);

    EXPECT_EQ(store.room_count(), 0u);
    EXPECT_TRUE(store.empty());
}

TEST(ChatStore, CreateRoomReportsWhetherItWasNew) {
    ChatStore store;
    EXPECT_TRUE(store.create_room("#general"));
    EXPECT_FALSE(store.create_room("#general"));
    EXPECT_EQ(store.room_count(), 1u);
}

TEST(ChatStore, DropRoomRemovesHistory) {
    ChatStore store;
    store.post("#general", Message("alice", "hello"));

    EXPECT_TRUE(store.drop_room("#general"));
    EXPECT_EQ(store.history("#general"), nullptr);
    EXPECT_FALSE(store.drop_room("#general"));
}

TEST(ChatStore, PostingTemporaryDoesNotCopy) {
    BasicChatStore<Tracked> store;
    Tracked::reset();

    store.post("#general", Tracked{std::string("payload")});

    EXPECT_EQ(Tracked::copies(), 0);
    EXPECT_GE(Tracked::moves(), 1);
}

TEST(ChatStore, PostingNamedMessageCopiesExactlyOnce) {
    BasicChatStore<Tracked> store;
    Tracked named{std::string("payload")};
    Tracked::reset();

    store.post("#general", named);

    EXPECT_EQ(Tracked::copies(), 1);
}

TEST(ChatStore, ExplicitMoveDoesNotCopy) {
    BasicChatStore<Tracked> store;
    Tracked named{std::string("payload")};
    Tracked::reset();

    store.post("#general", std::move(named));

    EXPECT_EQ(Tracked::copies(), 0);
}

TEST(ChatStore, ForEachRoomVisitsRoomsInNameOrder) {
    ChatStore store;
    store.post("#b", Message("u", "2"));
    store.post("#a", Message("u", "1"));
    store.post("#c", Message("u", "3"));

    std::vector<std::string> seen;
    store.for_each_room([&seen](const std::string& room, const ChatStore::History&) {
        seen.push_back(room);
    });

    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0], std::string("#a"));
    EXPECT_EQ(seen[1], std::string("#b"));
    EXPECT_EQ(seen[2], std::string("#c"));
}

TEST(ChatStore, CaptureByReferenceAccumulatesButByValueDoesNot) {
    ChatStore store;
    store.post("#a", Message("u", "1"));
    store.post("#a", Message("u", "2"));
    store.post("#b", Message("u", "3"));

    int by_reference = 0;
    store.for_each_room([&by_reference](const std::string&, const ChatStore::History& h) {
        by_reference += static_cast<int>(h.size());
    });

    int by_value = 0;
    store.for_each_room([by_value](const std::string&, const ChatStore::History& h) mutable {
        by_value += static_cast<int>(h.size());
    });

    EXPECT_EQ(by_reference, 3);
    EXPECT_EQ(by_value, 0);
}
