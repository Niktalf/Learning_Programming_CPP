// Этап 7, пункт 4: emplace против push и try_emplace против insert на типе,
// который записывает каждый вызов конструктора.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "traced.hpp"

using taskq::testing::count_events;
using taskq::testing::trace_log;
using taskq::testing::Traced;

namespace {

std::vector<std::string> events_of(void (*action)()) {
    trace_log().clear();
    action();
    return trace_log();
}

}  // namespace

TEST(Emplace, PushBackOfTemporaryConstructsAndMoves) {
    const auto events = events_of([] {
        std::vector<Traced> items;
        items.reserve(1);   // без перевыделения: меряется только вставка
        items.push_back(Traced("x"));
    });
    EXPECT_EQ(events, (std::vector<std::string>{"construct", "move"}));
}

TEST(Emplace, EmplaceBackConstructsInPlace) {
    const auto events = events_of([] {
        std::vector<Traced> items;
        items.reserve(1);
        items.emplace_back("x");
    });
    EXPECT_EQ(events, (std::vector<std::string>{"construct"}));
}

TEST(Emplace, EmplaceDoesNotRemoveACopyOfAVariable) {
    // emplace убирает временный объект, но не копию: переменная живёт
    // дальше, и забрать её ресурсы нельзя. Копию убирает только std::move.
    const auto copied = events_of([] {
        std::vector<Traced> items;
        items.reserve(1);
        const Traced variable("x");
        items.emplace_back(variable);
    });
    EXPECT_EQ(count_events("copy"), 1);
    EXPECT_EQ(copied, (std::vector<std::string>{"construct", "copy"}));
}

TEST(Emplace, ReallocationMovesOnlyBecauseMoveIsNoexcept) {
    // При перевыделении vector переносит элементы перемещением, только если
    // оно noexcept (move_if_noexcept). У Traced оно noexcept — копий нет.
    // Обратный случай, TracedThrowingMove, — звёздочка этапа 1,
    // tests/test_push_order.cpp.
    trace_log().clear();
    std::vector<Traced> items;
    for (int i = 0; i < 9; ++i) {
        items.emplace_back("x");
    }
    EXPECT_EQ(count_events("copy"), 0);
    EXPECT_GT(count_events("move"), 0);
}

TEST(Emplace, InsertBuildsThePairEvenWhenTheKeyExists) {
    std::map<std::string, Traced> map;
    map.try_emplace("k", "old");
    trace_log().clear();
    // insert получает готовую пару: значение построено и перемещено в неё
    // до того, как map узнал, что ключ занят, — и выброшено.
    map.insert({"k", Traced("new")});
    EXPECT_EQ(count_events("construct"), 1);
    EXPECT_GE(count_events("move") + count_events("copy"), 1);
    EXPECT_EQ(map.at("k").text(), std::string("old"));
}

TEST(Emplace, TryEmplaceConstructsNothingWhenTheKeyExists) {
    std::map<std::string, Traced> map;
    map.try_emplace("k", "old");
    trace_log().clear();
    // try_emplace сначала ищет ключ и строит значение, только если его нет.
    // Аргументы конструктора даже не трогаются.
    const auto [it, inserted] = map.try_emplace("k", "new");
    EXPECT_FALSE(inserted);
    EXPECT_TRUE(trace_log().empty());
    EXPECT_EQ(it->second.text(), std::string("old"));
}

TEST(Emplace, TryEmplaceOfNewKeyConstructsOnce) {
    std::map<std::string, Traced> map;
    trace_log().clear();
    map.try_emplace("k", "x");
    EXPECT_EQ(trace_log(), (std::vector<std::string>{"construct"}));
}

TEST(Emplace, SubscriptAssignDefaultConstructsFirst) {
    // Пункт 6 этапа: operator[] на map. Для нового ключа — пустое значение,
    // затем присваивание: два действия там, где хватило бы одного.
    std::map<std::string, Traced> map;
    trace_log().clear();
    map["k"] = Traced("x");
    EXPECT_EQ(trace_log(), (std::vector<std::string>{"construct", "default", "move-assign"}));
}
