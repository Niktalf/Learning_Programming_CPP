#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "store.hpp"
#include "tracked.hpp"

TEST(Store, SetGetDelete) {
    Store store;
    store.set("greeting", Value{std::string("hello")});

    const Value* found = store.get("greeting");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->str(), std::string("hello"));

    EXPECT_TRUE(store.del("greeting"));
    EXPECT_EQ(store.get("greeting"), nullptr);
    EXPECT_FALSE(store.del("greeting"));
}

TEST(Store, MissingKeyDoesNotCreateEntry) {
    Store store;
    EXPECT_EQ(store.get("absent"), nullptr);

    EXPECT_EQ(store.size(), 0u);
    EXPECT_TRUE(store.empty());
}

TEST(Store, SetOverwritesExistingKey) {
    Store store;
    store.set("k", Value{std::string("first")});
    store.set("k", Value{std::string("second")});

    ASSERT_NE(store.get("k"), nullptr);
    EXPECT_EQ(store.get("k")->str(), std::string("second"));
    EXPECT_EQ(store.size(), 1u);
}

TEST(Store, InsertingTemporaryDoesNotCopy) {
    BasicStore<Tracked> store;
    Tracked::reset();

    store.set("k", Tracked{std::string("payload")});

    EXPECT_EQ(Tracked::copies(), 0);
    EXPECT_GE(Tracked::moves(), 1);
}

TEST(Store, InsertingNamedValueCopiesExactlyOnce) {
    BasicStore<Tracked> store;
    Tracked named{std::string("payload")};
    Tracked::reset();

    store.set("k", named);

    EXPECT_EQ(Tracked::copies(), 1);
}

TEST(Store, ExplicitMoveDoesNotCopy) {
    BasicStore<Tracked> store;
    Tracked named{std::string("payload")};
    Tracked::reset();

    store.set("k", std::move(named));

    EXPECT_EQ(Tracked::copies(), 0);
}

TEST(Store, ForEachVisitsEveryEntryInKeyOrder) {
    Store store;
    store.set("b", Value{std::string("2")});
    store.set("a", Value{std::string("1")});
    store.set("c", Value{std::string("3")});

    std::vector<std::string> seen;
    store.for_each([&seen](const std::string& key, const Value&) {
        seen.push_back(key);
    });

    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0], std::string("a"));
    EXPECT_EQ(seen[1], std::string("b"));
    EXPECT_EQ(seen[2], std::string("c"));
}

TEST(Store, CaptureByReferenceAccumulatesButByValueDoesNot) {
    Store store;
    store.set("a", Value{std::string("1")});
    store.set("b", Value{std::string("22")});

    int by_reference = 0;
    store.for_each([&by_reference](const std::string&, const Value& v) {
        by_reference += static_cast<int>(v.size());
    });

    int by_value = 0;
    store.for_each([by_value](const std::string&, const Value& v) mutable {
        by_value += static_cast<int>(v.size());
    });

    EXPECT_EQ(by_reference, 3);
    EXPECT_EQ(by_value, 0);
}
