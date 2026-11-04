#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "chat/message.hpp"

using chat::Message;

TEST(Message, RuleOfZeroHoldsAtCompileTime) {
    // Всё это компилятор сгенерировал сам. Если кто-то объявит в Message
    // деструктор, перемещающие операции пропадут и первые две проверки упадут.
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<Message>);
    EXPECT_TRUE(std::is_nothrow_move_assignable_v<Message>);
    EXPECT_TRUE(std::is_copy_constructible_v<Message>);
    EXPECT_TRUE(std::is_copy_assignable_v<Message>);
    EXPECT_TRUE(std::is_nothrow_destructible_v<Message>);
}

TEST(Message, StoresAuthorAndText) {
    const Message a("alice", "hello");
    const Message b("alice", "hello");
    const Message c("bob", "hello");

    EXPECT_EQ(a.author(), std::string("alice"));
    EXPECT_EQ(a.to_string(), std::string("hello"));
    EXPECT_EQ(a.size(), 5u);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Message, ReportsAlternative) {
    const Message text("me", "hello");
    const Message reaction("me", chat::Body{chat::Reaction{"+1", 3}});

    EXPECT_EQ(text.type_name(), std::string_view("text"));
    EXPECT_EQ(reaction.type_name(), std::string_view("reaction"));
    // get_if, а не std::get: неверная альтернатива здесь штатная ситуация.
    EXPECT_EQ(text.get_if<chat::Reaction>(), nullptr);
    ASSERT_NE(reaction.get_if<chat::Reaction>(), nullptr);
    EXPECT_EQ(reaction.get_if<chat::Reaction>()->count, 3);
}

TEST(Message, MoveLeavesSourceUsable) {
    Message source("alice", "payload");
    const Message target{std::move(source)};

    EXPECT_EQ(target.to_string(), std::string("payload"));
    // Состояние перемещённого объекта не определено конкретным значением,
    // но объект обязан оставаться пригодным для использования и разрушения.
    EXPECT_TRUE(source.size() == 0 || source.size() == 7);
}
