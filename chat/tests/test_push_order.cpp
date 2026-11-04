// Звёздочка этапа 1 в виде теста: фиксирует, сколько копий и перемещений стоит
// каждый способ добавления в std::vector и перевыделение. Объяснение — в
// tools/push_order.cpp.

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "traced.hpp"

using chat::testing::Traced;
using chat::testing::TracedThrowingMove;
using chat::testing::count_events;
using chat::testing::trace_log;

TEST(PushOrder, TemporaryIsMovedNotCopied) {
    std::vector<Traced> items;
    items.reserve(1);
    trace_log().clear();

    items.push_back(Traced("x"));

    EXPECT_EQ(count_events("copy"), 0);
    EXPECT_EQ(count_events("move"), 1);
}

TEST(PushOrder, VariableIsCopiedAndStdMoveRemovesTheCopy) {
    std::vector<Traced> items;
    items.reserve(2);
    Traced variable("y");
    trace_log().clear();

    items.push_back(variable);
    EXPECT_EQ(count_events("copy"), 1);

    trace_log().clear();
    items.push_back(std::move(variable));
    EXPECT_EQ(count_events("copy"), 0);
    EXPECT_EQ(count_events("move"), 1);
}

TEST(PushOrder, EmplaceBackConstructsInPlace) {
    std::vector<Traced> items;
    items.reserve(1);
    trace_log().clear();

    items.emplace_back("e");

    ASSERT_EQ(trace_log().size(), 1u);
    EXPECT_EQ(trace_log().front(), std::string("construct"));
}

TEST(PushOrder, ReallocationMovesWhenMoveIsNoexcept) {
    static_assert(std::is_nothrow_move_constructible<Traced>::value, "");
    std::vector<Traced> items;
    items.reserve(2);
    items.emplace_back("a");
    items.emplace_back("b");
    trace_log().clear();

    items.emplace_back("c");

    EXPECT_EQ(count_events("move"), 2);
    EXPECT_EQ(count_events("copy"), 0);
}

TEST(PushOrder, ReallocationCopiesWhenMoveMayThrow) {
    static_assert(!std::is_nothrow_move_constructible<TracedThrowingMove>::value, "");
    std::vector<TracedThrowingMove> items;
    items.reserve(2);
    items.emplace_back("a");
    items.emplace_back("b");
    trace_log().clear();

    items.emplace_back("c");

    EXPECT_EQ(count_events("copy"), 2);
    EXPECT_EQ(count_events("move"), 0);
}
