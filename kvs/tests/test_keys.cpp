// Этап 5: KEYS ленивым конвейером. Главное — доказать ленивость счётчиком.

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "kvs/glob.hpp"
#include "kvs/store.hpp"

namespace {

kvs::Store hundred_keys() {
    kvs::Store store;
    for (int i = 0; i < 100; ++i) {
        store.set("key:" + std::to_string(1000 + i), kvs::Value(std::string("v")));
    }
    return store;
}

}  // namespace

TEST(Keys, PipelineIsLazy) {
    const kvs::Store store = hundred_keys();
    int calls = 0;
    const auto keys = store.keys_where(
        [&calls](std::string_view) {
            ++calls;
            return true;
        },
        3);
    ASSERT_EQ(keys.size(), 3u);
    // Предикат вызван четыре раза, а не сто: take остановил обход, как только
    // набралось три подходящих. Материализация до take дала бы 100.
    //
    // Почему четыре, а не три. Цикл после третьего элемента делает ++ у
    // итератора take, тот — у итератора filter, а filter, продвигаясь, ищет
    // следующий подходящий элемент и вызывает предикат ещё раз. Только потом
    // take сравнивает счётчик с пределом и останавливается. «Немногим больше
    // limit» — это limit + 1.
    EXPECT_EQ(calls, 4);
}

TEST(Keys, LazinessWithSparseMatches) {
    const kvs::Store store = hundred_keys();
    int calls = 0;
    // Подходит каждый десятый ключ; нужно два — значит, обход дойдёт до
    // второго подходящего и остановится.
    const auto keys = store.keys_where(
        [&calls](std::string_view key) {
            ++calls;
            return key.ends_with('0');
        },
        2);
    EXPECT_EQ(keys, (std::vector<std::string>{"key:1000", "key:1010"}));
    // 1000 … 1010 — одиннадцать вызовов, и ещё до следующего подходящего,
    // 1020, — то же продвижение filter после последнего элемента, что и выше.
    EXPECT_EQ(calls, 21);
}

TEST(Keys, FilterBeforeTakeIsNotTakeBeforeFilter) {
    const kvs::Store store = hundred_keys();
    // filter | take: два первых подходящих.
    EXPECT_EQ(store.keys("key:105*", 2), (std::vector<std::string>{"key:1050", "key:1051"}));
    // take | filter взял бы два первых ключа подряд — key:1000 и key:1001 —
    // и не нашёл бы среди них ни одного подходящего.
}

TEST(Keys, PatternAndLimit) {
    kvs::Store store;
    for (const char* const key : {"user:1", "user:2", "order:1", "user:10"}) {
        store.set(key, kvs::Value(std::string("v")));
    }
    EXPECT_EQ(store.keys("user:*", 10), (std::vector<std::string>{"user:1", "user:10", "user:2"}));
    EXPECT_EQ(store.keys("user:?", 10), (std::vector<std::string>{"user:1", "user:2"}));
    EXPECT_EQ(store.keys("*", 2).size(), 2u);
    EXPECT_TRUE(store.keys("nothing*", 10).empty());
    EXPECT_TRUE(store.keys("*", 0).empty());
}

TEST(Glob, Matching) {
    EXPECT_TRUE(kvs::glob_match("*", ""));
    EXPECT_TRUE(kvs::glob_match("a*b", "ab"));
    EXPECT_TRUE(kvs::glob_match("a*b", "axxxb"));
    EXPECT_TRUE(kvs::glob_match("?b", "ab"));
    EXPECT_FALSE(kvs::glob_match("?b", "b"));
    EXPECT_FALSE(kvs::glob_match("a*b", "axxxc"));
    EXPECT_TRUE(kvs::glob_match("*a*a*a*b", std::string(10000, 'a') + 'b'));
    EXPECT_FALSE(kvs::glob_match("*a*a*a*b", std::string(10000, 'a')));
}

TEST(Keys, HugeLimitIsClampedNotNegative) {
    // size_t больше PTRDIFF_MAX, переданный в views::take, стал бы
    // отрицательной разностью итераторов. Нашёл фаззер варианта B на том же
    // конвейере; здесь закрыто тем же способом.
    const kvs::Store store = hundred_keys();
    EXPECT_EQ(store.keys("*", static_cast<std::size_t>(-1)).size(), 100u);
}
