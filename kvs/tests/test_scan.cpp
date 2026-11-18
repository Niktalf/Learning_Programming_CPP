// Этап 8: SCAN с курсором и KEYSRANGE по упорядоченному индексу.

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "kvs/commands.hpp"
#include "kvs/store.hpp"

using kvs::Store;

namespace {

Store numbered(int count) {
    Store store;
    for (int i = 0; i < count; ++i) {
        store.set("key:" + std::to_string(1000 + i), kvs::Value(std::string("v")));
    }
    return store;
}

}  // namespace

TEST(Scan, FullIterationSeesEveryKeyExactlyOnce) {
    // Без записей между вызовами — каждый ключ ровно один раз, на любом
    // движке: страница — целые корзины, и корзины не пересекаются.
    const Store store = numbered(1000);
    std::multiset<std::string> seen;
    std::size_t cursor = 0;
    std::size_t calls = 0;
    do {
        const auto page = store.scan_page(cursor, 16, "*");
        seen.insert(page.keys.begin(), page.keys.end());
        cursor = page.next_cursor;
        ++calls;
    } while (cursor != 0);
    EXPECT_EQ(seen.size(), 1000u);
    EXPECT_EQ(std::set<std::string>(seen.begin(), seen.end()).size(), 1000u);
    EXPECT_GT(calls, 1u);   // обход по частям, а не одним вызовом
}

TEST(Scan, PatternFiltersEachPage) {
    const Store store = numbered(200);
    std::vector<std::string> matched;
    std::size_t cursor = 0;
    do {
        auto page = store.scan_page(cursor, 8, "key:10?5");
        matched.insert(matched.end(), page.keys.begin(), page.keys.end());
        cursor = page.next_cursor;
    } while (cursor != 0);
    std::ranges::sort(matched);
    EXPECT_EQ(matched, (std::vector<std::string>{"key:1005", "key:1015", "key:1025", "key:1035", "key:1045",
                                                 "key:1055", "key:1065", "key:1075", "key:1085", "key:1095"}));
}

TEST(Scan, ZeroCountStillMakesProgress) {
    const Store store = numbered(10);
    const auto page = store.scan_page(0, 0, "*");
    EXPECT_NE(page.next_cursor, 0u);   // хотя бы одна корзина за вызов
}

TEST(Scan, CommandPrintsCursorFirst) {
    Store store;
    static_cast<void>(kvs::execute(store, "SET only v"));
    std::string text = kvs::execute(store, "SCAN 0 * 100000").text;
    EXPECT_EQ(text, std::string("0\nonly"));
    EXPECT_EQ(kvs::execute(store, "SCAN 0 nothing* 100000").text, std::string("0"));
}

TEST(KeysRange, BoundsAreInclusiveAndOrdered) {
    Store store;
    for (const char* const key : {"b", "a", "d", "c", "e", "ca"}) {
        store.set(key, kvs::Value(std::string("v")));
    }
    EXPECT_EQ(store.keys_range("b", "d", 100), (std::vector<std::string>{"b", "c", "ca", "d"}));
    EXPECT_EQ(store.keys_range("bb", "cz", 100), (std::vector<std::string>{"c", "ca"}));
    EXPECT_EQ(store.keys_range("a", "z", 2), (std::vector<std::string>{"a", "b"}));
    EXPECT_TRUE(store.keys_range("x", "z", 100).empty());
    EXPECT_TRUE(store.keys_range("d", "b", 100).empty());   // перевёрнутый диапазон
}

TEST(KeysRange, IndexFollowsDeletesAndFailedWrites) {
    Store store(12);
    store.set("a", kvs::Value(std::string("1")));
    store.set("b", kvs::Value(std::string("2")));
    store.del("a");
    EXPECT_EQ(store.keys_range("a", "z", 10), (std::vector<std::string>{"b"}));
    // Запись за пределом памяти не прошла — и в индексе ключа нет.
    EXPECT_THROW(store.set("c", kvs::Value(std::string(100, 'x'))), kvs::OutOfMemoryError);
    EXPECT_EQ(store.keys_range("a", "z", 10), (std::vector<std::string>{"b"}));
}

TEST(KeysRange, Command) {
    Store store;
    static_cast<void>(kvs::execute(store, "MSET user:1 a user:2 b user:3 c order:1 d"));
    EXPECT_EQ(kvs::execute(store, "KEYSRANGE user:1 user:2").text, std::string("user:1\nuser:2"));
    EXPECT_EQ(kvs::execute(store, "KEYSRANGE user: user:~ 2").text, std::string("user:1\nuser:2"));
    EXPECT_EQ(kvs::execute(store, "KEYSRANGE x z").text, std::string("(empty)"));
}

TEST(KeysRange, CopyAndMsetKeepTheIndex) {
    // MSET — copy-and-swap: копия хранилища копирует и индекс, и обмен
    // меняет оба.
    Store store;
    static_cast<void>(kvs::execute(store, "MSET b 1 a 2"));
    EXPECT_EQ(store.keys_range("a", "b", 10), (std::vector<std::string>{"a", "b"}));
    const Store copy = store;
    store.del("a");
    EXPECT_EQ(copy.keys_range("a", "b", 10), (std::vector<std::string>{"a", "b"}));
}

TEST(Scan, ChainedEngineMissesNothingAcrossGrowth) {
    // Для собственной таблицы с цепочками гарантия сильнее, чем обещано в
    // общем случае: число корзин — степень двойки, рост — удвоение, и ключ
    // из корзины b переезжает в b или b + старое_число. Корзины не меньше
    // курсора переезжают в корзины не меньше курсора — ни один ключ, бывший
    // в хранилище с начала обхода, не пропадает, хотя может встретиться
    // дважды. Рассуждение — docs/hashing.md.
    Store store(kvs::EngineKind::Hash);
    for (int i = 0; i < 500; ++i) {
        store.set("key:" + std::to_string(i), kvs::Value(std::string("v")));
    }
    const std::size_t buckets_before = store.engine_bucket_count();
    std::set<std::string> seen;
    auto page = store.scan_page(0, 64, "*");
    seen.insert(page.keys.begin(), page.keys.end());
    // Посреди обхода — вставка, которая вынуждает рехеширование.
    for (int i = 0; i < 2000; ++i) {
        store.set("extra:" + std::to_string(i), kvs::Value(std::string("v")));
    }
    ASSERT_GT(store.engine_bucket_count(), buckets_before);
    while (page.next_cursor != 0) {
        page = store.scan_page(page.next_cursor, 64, "key:*");
        seen.insert(page.keys.begin(), page.keys.end());
    }
    for (int i = 0; i < 500; ++i) {
        EXPECT_TRUE(seen.contains("key:" + std::to_string(i))) << i;
    }
}
