// Этап 5: оператор <=> в двух вариантах.

#include <gtest/gtest.h>

#include <compare>
#include <string>
#include <type_traits>

#include "chat/chat_store.hpp"
#include "chat/message.hpp"
#include "chat/message_id.hpp"

using chat::MessageId;
using chat::PlainMessageId;

TEST(MessageId, DefaultComparesFieldsInDeclarationOrder) {
    EXPECT_LT((PlainMessageId{1, 100}), (PlainMessageId{2, 1}));
    EXPECT_LT((PlainMessageId{1, 1}), (PlainMessageId{1, 2}));
    EXPECT_EQ((PlainMessageId{1, 2}), (PlainMessageId{1, 2}));   // == выведен из = default
    EXPECT_TRUE((std::is_same_v<decltype(PlainMessageId{} <=> PlainMessageId{}),
                                std::strong_ordering>));
}

TEST(MessageId, EqualityIgnoresAuthor) {
    const MessageId alice{3, 10, "alice"};
    const MessageId bob{3, 10, "bob"};
    // Без отдельно объявленного operator== эта строка не скомпилировалась бы:
    // из написанного руками <=> равенство не выводится.
    EXPECT_EQ(alice, bob);
    EXPECT_EQ(alice <=> bob, std::strong_ordering::equal);
}

TEST(MessageId, OrderingIsSeqThenTimestamp) {
    EXPECT_LT((MessageId{1, 100, "a"}), (MessageId{2, 1, "a"}));
    EXPECT_LT((MessageId{2, 1, "z"}), (MessageId{2, 5, "a"}));
}

TEST(MessageId, StoreIssuesIncreasingIds) {
    chat::ChatStore store;
    const MessageId first = store.next_message_id("alice");
    const MessageId second = store.next_message_id("bob");
    EXPECT_LT(first, second);
    EXPECT_EQ(second.seq, first.seq + 1);
}

TEST(MessageId, CopiesForSeveralRoomsShareTheId) {
    chat::ChatStore store;
    store.create_room("#a");
    store.create_room("#b");
    chat::MessagePtr message = chat::make_text("alice", "hi");
    message->assign_id(store.next_message_id("alice"));
    store.post_many({"#a", "#b"}, std::move(message));

    const auto& in_a = *store.history("#a")->get().front();
    const auto& in_b = *store.history("#b")->get().front();
    // Два объекта, одно сообщение: clone скопировал идентификатор.
    EXPECT_NE(&in_a, &in_b);
    EXPECT_EQ(in_a.id(), in_b.id());
}
