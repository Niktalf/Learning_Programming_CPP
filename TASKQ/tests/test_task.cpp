#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

#include "task.hpp"


TEST(Task, RuleOfZeroHoldsAtCompileTime) {
    EXPECT_TRUE(std::is_nothrow_move_constructible<Task>::value);
    EXPECT_TRUE(std::is_nothrow_move_assignable<Task>::value);
    EXPECT_TRUE(std::is_copy_constructible<Task>::value);
    EXPECT_TRUE(std::is_copy_assignable<Task>::value);
    EXPECT_TRUE(std::is_nothrow_destructible<Task>::value);
}

TEST(Task, StoresFields) {
    const Task a(1, "make all", 5);
    const Task b(1, "make all", 5);
    const Task c(2, "make all", 5);

    EXPECT_EQ(a.id(), 1u);
    EXPECT_EQ(a.payload(), std::string("make all"));
    EXPECT_EQ(a.priority(), 5);
    EXPECT_EQ(a.size(), 8u);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Task, MoveLeavesSourceUsable) {
    Task source(1, "payload", 3);
    const Task target{std::move(source)};

    EXPECT_EQ(target.payload(), std::string("payload"));
    EXPECT_TRUE(source.size() == 0 || source.size() == 7);
}
