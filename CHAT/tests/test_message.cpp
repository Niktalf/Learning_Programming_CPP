#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "message.hpp"

TEST(Message, MoveIsNoexceptCopyIsNot) {
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<Message>);
    EXPECT_TRUE(std::is_nothrow_move_assignable_v<Message>);
    EXPECT_TRUE(std::is_copy_constructible_v<Message>);
    EXPECT_FALSE(std::is_nothrow_copy_constructible_v<Message>);
}

TEST(Message, StoresAuthorAndReportsType) {
    const Message text("alice", "hello");
    const Message reaction("bob", std::make_unique<ReactionMessage>("+1", 3));

    EXPECT_EQ(text.author(), std::string("alice"));
    EXPECT_EQ(text.to_string(), std::string("hello"));
    EXPECT_EQ(text.type_name(), std::string_view("text"));
    EXPECT_EQ(reaction.type_name(), std::string_view("reaction"));
}

TEST(Message, AsReturnsNullptrOnWrongType) {
    const Message text("alice", "hello");
    const Message reaction("bob", std::make_unique<ReactionMessage>("+1", 3));

    EXPECT_EQ(text.as<ReactionMessage>(), nullptr);
    ASSERT_NE(reaction.as<ReactionMessage>(), nullptr);
    EXPECT_EQ(reaction.as<ReactionMessage>()->count(), 3);
}

TEST(Message, CopyIsDeepAndIndependent) {
    Message original("bob", std::make_unique<ReactionMessage>("+1", 1));
    Message copy = original;

    EXPECT_NE(copy.as<ReactionMessage>(), original.as<ReactionMessage>());

    copy.as<ReactionMessage>()->add(9);

    EXPECT_EQ(original.as<ReactionMessage>()->count(), 1);
    EXPECT_EQ(copy.as<ReactionMessage>()->count(), 10);
}

TEST(Message, MoveTransfersTheSameObject) {
    Message source("alice", "payload");
    const TextMessage* address = source.as<TextMessage>();
    const Message target(std::move(source));

    EXPECT_EQ(target.as<TextMessage>(), address);
}

TEST(Message, EqualityComparesAuthorAndContent) {
    EXPECT_TRUE(Message("a", "x") == Message("a", "x"));
    EXPECT_TRUE(Message("a", "x") != Message("b", "x"));
    EXPECT_TRUE(Message("a", "x") != Message("a", std::make_unique<SystemMessage>("x")));
}
