#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "value.hpp"

TEST(Value, RuleOfZeroHoldsAtCompileTime) {
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<Value>);
    EXPECT_TRUE(std::is_nothrow_move_assignable_v<Value>);
    EXPECT_TRUE(std::is_copy_constructible_v<Value>);
    EXPECT_TRUE(std::is_copy_assignable_v<Value>);
    EXPECT_TRUE(std::is_nothrow_destructible_v<Value>);
}

TEST(Value, StoresAndComparesText) {
    const Value a{std::string("hello")};
    const Value b{std::string("hello")};
    const Value c{std::string("world")};

    EXPECT_EQ(a.to_string(), std::string("hello"));
    EXPECT_EQ(a.size(), 5u);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Value, HoldsDifferentAlternatives) {
    const Value text(std::string("hello"));
    const Value number(std::int64_t{42});
    const Value real(2.5);

    EXPECT_EQ(text.type_name(), std::string_view("string"));
    EXPECT_EQ(number.type_name(), std::string_view("int"));
    EXPECT_EQ(real.type_name(), std::string_view("float"));
    EXPECT_EQ(number.to_string(), std::string("42"));

    EXPECT_EQ(text.get_if<std::int64_t>(), nullptr);
    ASSERT_NE(number.get_if<std::int64_t>(), nullptr);
    EXPECT_EQ(*number.get_if<std::int64_t>(), 42);

    EXPECT_TRUE(Value(std::int64_t{1}) != Value(1.0));
}

TEST(Value, MoveLeavesSourceUsable) {
    Value source{std::string("payload")};
    const Value target{std::move(source)};

    EXPECT_EQ(target.to_string(), std::string("payload"));
    EXPECT_TRUE(source.size() == 0 || source.size() == 7);
}
