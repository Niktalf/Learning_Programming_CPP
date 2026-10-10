#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "value.hpp"

TEST(Value, MoveIsNoexceptCopyIsNot) {
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<Value>);
    EXPECT_TRUE(std::is_nothrow_move_assignable_v<Value>);
    EXPECT_TRUE(std::is_copy_constructible_v<Value>);
    EXPECT_FALSE(std::is_nothrow_copy_constructible_v<Value>);
}

TEST(Value, ReportsTypeOfEveryAlternative) {
    EXPECT_EQ(Value(std::string("hello")).type_name(), std::string_view("string"));
    EXPECT_EQ(Value(std::int64_t{42}).type_name(), std::string_view("int"));
    EXPECT_EQ(Value(2.5).type_name(), std::string_view("float"));
    EXPECT_EQ(Value(std::make_unique<ListValue>()).type_name(), std::string_view("list"));
}

TEST(Value, AsReturnsNullptrOnWrongType) {
    const Value text(std::string("hello"));
    const Value number(std::int64_t{42});

    EXPECT_EQ(text.as<IntValue>(), nullptr);
    ASSERT_NE(number.as<IntValue>(), nullptr);
    EXPECT_EQ(number.as<IntValue>()->data(), 42);
}

TEST(Value, CopyIsDeepAndIndependent) {
    Value original(std::int64_t{1});
    Value copy = original;

    ASSERT_NE(copy.as<IntValue>(), nullptr);
    EXPECT_NE(copy.as<IntValue>(), original.as<IntValue>());

    copy.as<IntValue>()->add(99);

    EXPECT_EQ(original.as<IntValue>()->data(), 1);
    EXPECT_EQ(copy.as<IntValue>()->data(), 100);
}

TEST(Value, MoveTransfersTheSameObject) {
    Value source(std::string("payload"));
    const StringValue* address = source.as<StringValue>();
    const Value target(std::move(source));

    EXPECT_EQ(target.as<StringValue>(), address);
}

TEST(Value, SelfAssignmentIsSafe) {
    Value value(std::string("payload"));
    Value& alias = value;
    value = alias;

    EXPECT_EQ(value.to_string(), std::string("payload"));
}

TEST(Value, EqualityComparesTypeAndContent) {
    EXPECT_TRUE(Value(std::int64_t{1}) == Value(std::int64_t{1}));
    EXPECT_TRUE(Value(std::int64_t{1}) != Value(std::int64_t{2}));
    EXPECT_TRUE(Value(std::int64_t{1}) != Value(1.0));
}
