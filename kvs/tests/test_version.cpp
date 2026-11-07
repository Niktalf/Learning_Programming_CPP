// Этап 5: оператор <=> в двух вариантах.

#include <gtest/gtest.h>

#include <compare>
#include <string>
#include <type_traits>

#include "kvs/store.hpp"
#include "kvs/version.hpp"

using kvs::PlainVersion;
using kvs::RecordVersion;

TEST(Version, DefaultComparesFieldsInDeclarationOrder) {
    // Поколение важнее времени: так объявлены поля.
    EXPECT_LT((PlainVersion{1, 100}), (PlainVersion{2, 1}));
    EXPECT_LT((PlainVersion{1, 1}), (PlainVersion{1, 2}));
    // operator== выведен из = default.
    EXPECT_EQ((PlainVersion{1, 2}), (PlainVersion{1, 2}));
    EXPECT_TRUE((std::is_same_v<decltype(PlainVersion{} <=> PlainVersion{}), std::strong_ordering>));
}

TEST(Version, EqualityIgnoresAuthor) {
    const RecordVersion alice{3, 10, "alice"};
    const RecordVersion bob{3, 10, "bob"};
    // Автор в сравнении не участвует: это одна и та же версия записи. Без
    // отдельно объявленного operator== эта строка не скомпилировалась бы —
    // из написанного руками <=> равенство не выводится.
    EXPECT_EQ(alice, bob);
    EXPECT_EQ(alice <=> bob, std::strong_ordering::equal);
}

TEST(Version, OrderingIsGenerationThenTimestamp) {
    EXPECT_LT((RecordVersion{1, 100, "a"}), (RecordVersion{2, 1, "a"}));
    EXPECT_LT((RecordVersion{2, 1, "z"}), (RecordVersion{2, 5, "a"}));
    EXPECT_GT((RecordVersion{2, 5, "a"}), (RecordVersion{2, 1, "z"}));
}

TEST(Version, StoreBumpsGenerationOnEveryWrite) {
    kvs::Store store;
    store.set("k", kvs::Value(std::string("1")));
    store.set("other", kvs::Value(std::string("x")));
    store.set("k", kvs::Value(std::string("2")), "alice");

    const auto version = store.version("k");
    ASSERT_TRUE(version.has_value());
    EXPECT_EQ(version->generation, 2u);
    EXPECT_EQ(version->timestamp, 3u);   // третья запись в хранилище
    EXPECT_EQ(version->author, std::string("alice"));

    // Более поздняя запись — более новая версия.
    EXPECT_GT(*store.version("k"), *store.version("other"));
}

TEST(Version, DeletedKeyLosesItsVersion) {
    kvs::Store store;
    store.set("k", kvs::Value(std::string("1")));
    store.del("k");
    EXPECT_FALSE(store.version("k").has_value());
    store.set("k", kvs::Value(std::string("2")));
    EXPECT_EQ(store.version("k")->generation, 1u);   // история ключа начата заново
}
