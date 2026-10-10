#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "task.hpp"


TEST(Task, RuleOfZeroHoldsAtCompileTime) {
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<Task>);
    EXPECT_TRUE(std::is_nothrow_move_assignable_v<Task>);
    EXPECT_TRUE(std::is_copy_constructible_v<Task>);
    EXPECT_TRUE(std::is_copy_assignable_v<Task>);
    EXPECT_TRUE(std::is_nothrow_destructible_v<Task>);
}

TEST(Task, StoresFields) {
    const Task a(1, "make all", 5);
    const Task b(1, "make all", 5);
    const Task c(2, "make all", 5);

    EXPECT_EQ(a.id(), 1u);
    EXPECT_EQ(a.to_string(), std::string("make all"));
    EXPECT_EQ(a.priority(), 5);
    EXPECT_EQ(a.size(), 8u);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Task, ReportsAlternative) {
    const Task text(1, "make all");
    const Task number(2, Payload{
        NumericPayload{
            .value = 7
        }
    });

    EXPECT_EQ(text.type_name(), std::string_view("text"));
    EXPECT_EQ(number.type_name(), std::string_view("number"));
    EXPECT_EQ(text.get_if<NumericPayload>(), nullptr);
    ASSERT_NE(number.get_if<NumericPayload>(), nullptr);
    EXPECT_EQ(number.get_if<NumericPayload>()->value, 7);
}

TEST(Task, MoveLeavesSourceUsable) {
    Task source(1, "payload", 3);
    const Task target{std::move(source)};

    EXPECT_EQ(target.to_string(), std::string("payload"));
    EXPECT_TRUE(source.size() == 0 || source.size() == 7);
}
