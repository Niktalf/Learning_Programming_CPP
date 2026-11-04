// Звёздочка этапа 1 в виде теста: фиксирует, сколько копий и перемещений
// стоит каждый способ вставки в std::map. Объяснение — в tools/insert_order.cpp.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "traced.hpp"

using kvs::testing::Traced;
using kvs::testing::count_events;
using kvs::testing::trace_log;

namespace {

using Map = std::map<std::string, Traced>;

}  // namespace

TEST(InsertOrder, TemporaryIsMovedTwiceAndNeverCopied) {
    Map map;
    trace_log().clear();

    map.insert(std::make_pair(std::string("k"), Traced("x")));

    EXPECT_EQ(count_events("construct"), 1);
    EXPECT_EQ(count_events("copy"), 0);
    EXPECT_EQ(count_events("move"), 2);
}

TEST(InsertOrder, VariableIsCopiedOnce) {
    Map map;
    const Traced variable("y");
    trace_log().clear();

    map.insert(std::make_pair(std::string("k"), variable));

    EXPECT_EQ(count_events("copy"), 1);
    EXPECT_EQ(count_events("move"), 1);
}

TEST(InsertOrder, StdMoveRemovesTheCopy) {
    Map map;
    Traced variable("z");
    trace_log().clear();

    map.insert(std::make_pair(std::string("k"), std::move(variable)));

    EXPECT_EQ(count_events("copy"), 0);
    EXPECT_EQ(count_events("move"), 2);
}

TEST(InsertOrder, PiecewiseEmplaceConstructsInPlace) {
    Map map;
    trace_log().clear();

    map.emplace(std::piecewise_construct, std::forward_as_tuple("k"),
                std::forward_as_tuple("e"));

    ASSERT_EQ(trace_log().size(), 1u);
    EXPECT_EQ(trace_log().front(), std::string("construct"));
}

TEST(InsertOrder, SubscriptDefaultConstructsThenAssigns) {
    Map map;
    trace_log().clear();

    map["k"] = Traced("w");

    EXPECT_EQ(count_events("default"), 1);
    EXPECT_EQ(count_events("move-assign"), 1);
}
