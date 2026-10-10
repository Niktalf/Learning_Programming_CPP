#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "task.hpp"


TEST(Task, MoveIsNoexceptCopyIsNot) {
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<Task>);
    EXPECT_TRUE(std::is_nothrow_move_assignable_v<Task>);
    EXPECT_TRUE(std::is_copy_constructible_v<Task>);
    EXPECT_FALSE(std::is_nothrow_copy_constructible_v<Task>);
}

TEST(Task, StoresFieldsAndReportsType) {
    const Task text(1, "make all", 5);
    const Task number(2, std::make_unique<NumericPayload>(7));

    EXPECT_EQ(text.id(), 1u);
    EXPECT_EQ(text.priority(), 5);
    EXPECT_EQ(text.to_string(), std::string("make all"));
    EXPECT_EQ(text.type_name(), std::string_view("text"));
    EXPECT_EQ(number.type_name(), std::string_view("number"));
}

TEST(Task, AsReturnsNullptrOnWrongType) {
    const Task text(1, "make all");
    const Task number(2, std::make_unique<NumericPayload>(7));
    EXPECT_EQ(text.as<NumericPayload>(), nullptr);
    ASSERT_NE(number.as<NumericPayload>(), nullptr);
    EXPECT_EQ(number.as<NumericPayload>()->value(), 7);
}

TEST(Task, CopyIsDeepAndIndependent) {
    Task original(1, std::make_unique<NumericPayload>(1));
    Task copy = original;

    EXPECT_NE(copy.as<NumericPayload>(), original.as<NumericPayload>());
    copy.as<NumericPayload>()->add(9);
    EXPECT_EQ(original.as<NumericPayload>()->value(), 1);
    EXPECT_EQ(copy.as<NumericPayload>()->value(), 10);
}

TEST(Task, MoveTransfersTheSameObject) {
    Task source(1, "payload");
    const TextPayload* address = source.as<TextPayload>();
    const Task target(std::move(source));
    EXPECT_EQ(target.as<TextPayload>(), address);
}

TEST(Task, SelfAssignmentIsSafe) {
    Task task(1, "payload");
    Task& alias = task;
    task = alias;
    EXPECT_EQ(task.to_string(), std::string("payload"));
}
