// Этап 7: список на std::deque и команды LPUSH, RPUSH, LPOP, RPOP, LRANGE.

#include <gtest/gtest.h>

#include <exception>
#include <string>
#include <vector>

#include "kvs/commands.hpp"
#include "kvs/errors.hpp"
#include "kvs/ivalue.hpp"

using kvs::ListValue;
using kvs::Store;

namespace {

std::string run(Store& store, const std::string& line) {
    return kvs::execute(store, line).text;
}

template <class E>
bool fails_with(Store& store, const std::string& line) {
    try {
        static_cast<void>(kvs::execute(store, line));
    } catch (...) {
        try {
            std::rethrow_exception(kvs::root_cause(std::current_exception()));
        } catch (const E&) {
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

ListValue abcde() {
    ListValue list;
    for (const char* const item : {"a", "b", "c", "d", "e"}) {
        list.push_back(item);
    }
    return list;
}

}  // namespace

TEST(List, PushAndPopOnBothEnds) {
    ListValue list;
    list.push_back("b");
    list.push_front("a");
    list.push_back("c");
    EXPECT_EQ(list.to_string(), std::string("[a, b, c]"));
    EXPECT_EQ(list.size(), 3u);   // объём — сумма длин
    EXPECT_EQ(list.pop_front(), std::string("a"));
    EXPECT_EQ(list.pop_back(), std::string("c"));
    EXPECT_EQ(list.length(), 1u);
    EXPECT_EQ(list.size(), 1u);
}

TEST(List, RangeLikeRedis) {
    const ListValue list = abcde();
    using Items = std::vector<std::string>;
    EXPECT_EQ(list.range(0, -1), (Items{"a", "b", "c", "d", "e"}));
    EXPECT_EQ(list.range(1, 2), (Items{"b", "c"}));
    EXPECT_EQ(list.range(-2, -1), (Items{"d", "e"}));
    EXPECT_EQ(list.range(-100, 1), (Items{"a", "b"}));   // до начала — обрезается
    EXPECT_EQ(list.range(3, 100), (Items{"d", "e"}));    // за конец — тоже
    EXPECT_TRUE(list.range(3, 1).empty());
    EXPECT_TRUE(list.range(5, 10).empty());
    EXPECT_TRUE(ListValue().range(0, -1).empty());
}

TEST(List, Commands) {
    Store store;
    EXPECT_EQ(run(store, "RPUSH l b"), std::string("1"));
    EXPECT_EQ(run(store, "LPUSH l a"), std::string("2"));
    EXPECT_EQ(run(store, "RPUSH l c"), std::string("3"));
    EXPECT_EQ(run(store, "LRANGE l 0 -1"), std::string("a\nb\nc"));
    EXPECT_EQ(run(store, "LRANGE l -1 -1"), std::string("c"));
    EXPECT_EQ(run(store, "LRANGE l 5 9"), std::string("(empty)"));
    EXPECT_EQ(run(store, "LPOP l"), std::string("a"));
    EXPECT_EQ(run(store, "RPOP l"), std::string("c"));
    EXPECT_EQ(run(store, "LLEN l"), std::string("1"));
}

TEST(List, PoppingTheLastItemDeletesTheKey) {
    Store store;
    run(store, "RPUSH l only");
    EXPECT_EQ(run(store, "LPOP l"), std::string("only"));
    EXPECT_FALSE(store.contains("l"));
    EXPECT_EQ(run(store, "LPOP l"), std::string("(nil)"));
    EXPECT_EQ(run(store, "LRANGE l 0 -1"), std::string("(empty)"));
    EXPECT_EQ(store.used_bytes(), 0u);
}

TEST(List, WrongTypeIsAnError) {
    Store store;
    run(store, "SET s text");
    for (const char* const line : {"LPUSH s x", "RPUSH s x", "LPOP s", "RPOP s", "LRANGE s 0 1", "LLEN s"}) {
        EXPECT_TRUE(fails_with<kvs::TypeMismatchError>(store, line)) << line;
    }
    EXPECT_EQ(run(store, "GET s"), std::string("text"));
}

TEST(List, ChangedInPlaceWithAccountingAndVersion) {
    // Этап 7: список меняется на месте — Store::modify, — а учёт объёма и
    // версия записи остаются честными.
    Store store;
    run(store, "RPUSH l abc");
    const std::size_t after_first = store.used_bytes();
    const auto first = store.version("l");
    run(store, "LPUSH l de");
    EXPECT_EQ(store.used_bytes(), after_first + 2);
    ASSERT_TRUE(first && store.version("l"));
    EXPECT_EQ(store.version("l")->generation, first->generation + 1);
    run(store, "RPOP l");
    EXPECT_EQ(store.used_bytes(), after_first - 1);   // осталось «de»: ключ 1 + 2
}

TEST(List, PushOverMemoryLimitChangesNothing) {
    Store store(16);
    run(store, "RPUSH l abc");
    EXPECT_TRUE(fails_with<kvs::OutOfMemoryError>(store, "LPUSH l " + std::string(20, 'x')));
    EXPECT_EQ(run(store, "LRANGE l 0 -1"), std::string("abc"));
}

TEST(List, ReferencesSurvivePushesAtBothEnds) {
    // Свойство deque, на котором стоит выбор: вставка в любой конец
    // делает недействительными итераторы, но не ссылки и указатели на
    // элементы. У vector вставка с перевыделением сдвинула бы и их.
    ListValue list = abcde();
    const std::string* const middle = &list.items()[2];
    for (int i = 0; i < 1000; ++i) {
        list.push_front("f");
        list.push_back("b");
    }
    EXPECT_EQ(middle, &list.items()[1002]);
    EXPECT_EQ(*middle, std::string("c"));
}
