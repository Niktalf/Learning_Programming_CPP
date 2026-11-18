// Этап 7: собственный итератор хранилища и звёздочка — ленивый scan со
// стражем.

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <set>
#include <string>
#include <vector>

#include "kvs/store.hpp"

using kvs::Store;
using StoreIterator = Store::Iterator;
using Scan = decltype(std::declval<const Store&>().scan(""));

// Главная проверка этапа — при компиляции.
static_assert(std::forward_iterator<StoreIterator>);
static_assert(std::ranges::forward_range<const Store>);
static_assert(std::is_same_v<std::iter_reference_t<StoreIterator>, const std::string&>);

// Звёздочка: однопроходный итератор и отдельный тип конца.
static_assert(std::input_iterator<Store::ScanIterator>);
static_assert(!std::forward_iterator<Store::ScanIterator>);
static_assert(std::sentinel_for<Store::ScanEnd, Store::ScanIterator>);
static_assert(std::ranges::input_range<Scan>);
static_assert(!std::ranges::forward_range<Scan>);
static_assert(!std::ranges::common_range<Scan>);   // конец — другого типа

namespace {

Store sample() {
    Store store;
    for (const char* const key : {"user:1", "user:2", "order:1", "user:10", "cart:7"}) {
        store.set(key, kvs::Value(std::string("v")));
    }
    return store;
}

}  // namespace

TEST(StoreIterator, RangeBasedForVisitsEveryKeyOnce) {
    const Store store = sample();
    std::multiset<std::string> seen;
    for (const std::string& key : store) {
        seen.insert(key);
    }
    EXPECT_EQ(seen, (std::multiset<std::string>{"cart:7", "order:1", "user:1", "user:10", "user:2"}));
}

TEST(StoreIterator, WorksWithStandardAlgorithms) {
    const Store store = sample();
    EXPECT_EQ(std::distance(store.begin(), store.end()), 5);
    EXPECT_EQ(std::count_if(store.begin(), store.end(),
                            [](const std::string& key) { return key.starts_with("user:"); }),
              3);
    EXPECT_NE(std::find(store.begin(), store.end(), "order:1"), store.end());
    EXPECT_EQ(std::ranges::find(store, "absent"), store.end());
    const auto longest = std::ranges::max_element(store, {}, &std::string::size);
    EXPECT_EQ(longest->size(), 7u);   // user:10 или order:1 — оба по семь
}

TEST(StoreIterator, IsMultiPass) {
    // Отличие forward от input: копия итератора проходит тот же путь.
    const Store store = sample();
    StoreIterator first = store.begin();
    StoreIterator copy = first;
    ++first;
    ++first;
    EXPECT_EQ(*copy, *store.begin());
    std::advance(copy, 2);
    EXPECT_EQ(copy, first);
    const StoreIterator before = copy++;
    EXPECT_EQ(before, first);
}

TEST(StoreIterator, StageFivePipelineRunsOnIt) {
    const Store store = sample();
    auto users = store | std::views::filter([](const std::string& key) { return key.starts_with("user:"); }) |
                 std::views::transform([](const std::string& key) { return key.size(); });
    EXPECT_EQ(std::ranges::fold_left(users, 0u, std::plus<>{}), 6u + 6u + 7u);
}

TEST(Scan, LazyAndSinglePass) {
    const Store store = sample();
    std::vector<std::string> users;
    for (const std::string& key : store.scan("user:*")) {
        users.push_back(key);
    }
    std::ranges::sort(users);
    EXPECT_EQ(users, (std::vector<std::string>{"user:1", "user:10", "user:2"}));
    // Ничего не подошло — begin сразу равен стражу.
    EXPECT_TRUE(store.scan("nothing*").begin() == Store::ScanEnd{});
}

TEST(Scan, StageFivePipelinesWorkUnchanged) {
    // Звёздочка: filter и take этапа 5 поверх input-диапазона со стражем.
    // Конвейеру не важно, что за итератор внизу, — лишь бы модель концепции.
    const Store store = sample();
    auto short_users = store.scan("user:*") |
                       std::views::filter([](const std::string& key) { return key.size() == 6; }) |
                       std::views::take(5);
    std::vector<std::string> taken;
    for (const std::string& key : short_users) {
        taken.push_back(key);
    }
    std::ranges::sort(taken);
    EXPECT_EQ(taken, (std::vector<std::string>{"user:1", "user:2"}));
}

TEST(Scan, FollowsTableOrderLikeTheForwardIterator) {
    // scan и прямой итератор идут по одной таблице в одном порядке: первый
    // ключ scan("*") — первый ключ обхода хранилища. take(1) берёт его и
    // останавливается: следующий подходящий ключ scan ищет только при ++.
    const Store store = sample();
    auto first = store.scan("*") | std::views::take(1);
    std::vector<std::string> taken(1);
    for (const std::string& key : first) {
        taken[0] = key;
    }
    EXPECT_EQ(taken[0], *store.begin());
}
