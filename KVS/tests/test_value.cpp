#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

#include "value.hpp"

TEST(Value, RuleOfZeroHoldsAtCompileTime) {
    EXPECT_TRUE(std::is_nothrow_move_constructible<Value>::value);
    EXPECT_TRUE(std::is_nothrow_move_assignable<Value>::value);
    EXPECT_TRUE(std::is_copy_constructible<Value>::value);
    EXPECT_TRUE(std::is_copy_assignable<Value>::value);
    EXPECT_TRUE(std::is_nothrow_destructible<Value>::value);
}

TEST(Value, StoresAndComparesText) {
    const Value a{std::string("hello")};
    const Value b{std::string("hello")};
    const Value c{std::string("world")};

    EXPECT_EQ(a.str(), std::string("hello"));
    EXPECT_EQ(a.size(), 5u);
    EXPECT_FALSE(a.empty());
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Value, MoveLeavesSourceUsable) {
    Value source{std::string("payload")};
    const Value target{std::move(source)};

    EXPECT_EQ(target.str(), std::string("payload"));
    EXPECT_TRUE(source.size() == 0 || source.size() == 7);
}
