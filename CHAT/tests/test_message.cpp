#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

#include "message.hpp"

TEST(Message, RuleOfZeroHoldsAtCompileTime) {
    EXPECT_TRUE(std::is_nothrow_move_constructible<Message>::value);
    EXPECT_TRUE(std::is_nothrow_move_assignable<Message>::value);
    EXPECT_TRUE(std::is_copy_constructible<Message>::value);
    EXPECT_TRUE(std::is_copy_assignable<Message>::value);
    EXPECT_TRUE(std::is_nothrow_destructible<Message>::value);
}

TEST(Message, StoresAuthorAndText) {
    const Message a("alice", "hello");
    const Message b("alice", "hello");
    const Message c("bob", "hello");

    EXPECT_EQ(a.author(), std::string("alice"));
    EXPECT_EQ(a.text(), std::string("hello"));
    EXPECT_EQ(a.size(), 5u);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Message, MoveLeavesSourceUsable) {
    Message source("alice", "payload");
    const Message target{std::move(source)};

    EXPECT_EQ(target.text(), std::string("payload"));
    EXPECT_TRUE(source.size() == 0 || source.size() == 7);
}
