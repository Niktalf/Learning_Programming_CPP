// Этап 8: собственные хеш-таблицы и хеш-функции сами по себе.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kvs/chained_table.hpp"
#include "kvs/hash.hpp"
#include "kvs/open_table.hpp"

using kvs::CharSumHash;
using kvs::Fnv1aHash;

namespace {

using Chained = kvs::ChainedTable<std::string, int, Fnv1aHash>;
using Open = kvs::OpenTable<std::string, int, Fnv1aHash>;

std::string key_of(int i) {
    return "user:" + std::to_string(i);
}

}  // namespace

TEST(Fnv1a, KnownVectorsAndSeed) {
    // Тестовые векторы FNV-1a 64 из описания алгоритма.
    EXPECT_EQ(kvs::fnv1a(""), 0xcbf29ce484222325ULL);
    EXPECT_EQ(kvs::fnv1a("a"), 0xaf63dc4c8601ec8cULL);
    EXPECT_EQ(kvs::fnv1a("foobar"), 0x85944171f73967e8ULL);
    // Затравка меняет значение, но не детерминированность.
    EXPECT_NE(kvs::fnv1a("key", 1), kvs::fnv1a("key", 2));
    EXPECT_EQ(kvs::fnv1a("key", 7), kvs::fnv1a("key", 7));
}

TEST(CharSum, AnagramsCollide) {
    EXPECT_EQ(kvs::char_sum("user:12"), kvs::char_sum("user:21"));
    EXPECT_EQ(kvs::char_sum("abc"), kvs::char_sum("cab"));
}

TEST(ChainedTable, InsertFindEraseAndGrow) {
    Chained table;
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(table.insert_or_assign(key_of(i), i));
    }
    EXPECT_FALSE(table.insert_or_assign(key_of(5), 500));   // замена, не вставка
    EXPECT_EQ(table.size(), 1000u);
    EXPECT_EQ(*table.find(std::string_view("user:5")), 500);
    EXPECT_LE(table.load_factor(), table.max_load_factor());
    EXPECT_EQ(table.bucket_count(), 1024u);   // удвоения от 8: порог 1.0
    for (int i = 0; i < 1000; i += 2) {
        EXPECT_TRUE(table.erase(key_of(i)));
    }
    EXPECT_FALSE(table.erase(std::string_view("user:0")));
    EXPECT_EQ(table.size(), 500u);
    EXPECT_EQ(table.find(std::string_view("user:0")), nullptr);
    EXPECT_EQ(*table.find(std::string_view("user:1")), 1);
}

TEST(ChainedTable, ValuesDoNotMoveOnRehash) {
    // Метод цепочек на списках: рехеширование перевешивает узлы, а не
    // переносит значения, и указатель на значение переживает рост таблицы.
    Chained table;
    table.insert_or_assign("first", 1);
    const int* const first = table.find(std::string_view("first"));
    for (int i = 0; i < 10000; ++i) {
        table.insert_or_assign(key_of(i), i);
    }
    EXPECT_EQ(table.find(std::string_view("first")), first);
}

TEST(ChainedTable, LongChainIsDestroyedWithoutRecursion) {
    // С суммой кодов десятки тысяч ключей ложатся в несколько корзин.
    // Рекурсивное разрушение цепочки из unique_ptr переполнило бы стек;
    // деструктор таблицы разрушает её циклом.
    kvs::ChainedTable<std::string, int, CharSumHash> table(CharSumHash{}, 1e9);
    for (int i = 0; i < 100000; ++i) {
        table.insert_or_assign(std::to_string(1000000 + i), i);
    }
    EXPECT_EQ(table.size(), 100000u);
    EXPECT_EQ(table.bucket_count(), 8u);   // порог огромный — роста нет
}

TEST(ChainedTable, CopyIsDeep) {
    Chained table;
    table.insert_or_assign("a", 1);
    Chained copy = table;
    *copy.find(std::string_view("a")) = 2;
    EXPECT_EQ(*table.find(std::string_view("a")), 1);
}

TEST(ChainedTable, SlotsVisitEveryKeyOnce) {
    Chained table;
    for (int i = 0; i < 300; ++i) {
        table.insert_or_assign(key_of(i), i);
    }
    std::vector<int> seen(300, 0);
    for (auto slot = table.first_from(0); slot; slot = table.next(*slot)) {
        ++seen[static_cast<std::size_t>(table.value_at(*slot))];
    }
    EXPECT_EQ(std::ranges::count(seen, 1), 300);
}

TEST(OpenTable, InsertFindEraseAndGrow) {
    Open table;
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(table.insert_or_assign(key_of(i), i));
    }
    EXPECT_EQ(table.size(), 1000u);
    EXPECT_LE(table.load_factor(), 0.75);
    for (int i = 0; i < 1000; i += 2) {
        EXPECT_TRUE(table.erase(key_of(i)));
    }
    EXPECT_EQ(table.size(), 500u);
    for (int i = 1; i < 1000; i += 2) {
        ASSERT_NE(table.find(key_of(i)), nullptr) << i;   // найдены мимо надгробий
    }
}

TEST(OpenTable, FindWalksPastTombstones) {
    // Три ключа с одной домашней ячейкой лягут подряд. Удаляем первый:
    // если бы ячейка стала пустой, поиск второго и третьего остановился бы
    // на ней. Надгробие говорит «иди дальше».
    struct SameHome {
        std::size_t operator()(std::string_view) const noexcept { return 3; }
    };
    kvs::OpenTable<std::string, int, SameHome> table;
    table.insert_or_assign("a", 1);
    table.insert_or_assign("b", 2);
    table.insert_or_assign("c", 3);
    EXPECT_TRUE(table.erase(std::string_view("a")));
    EXPECT_EQ(table.tombstones(), 1u);
    EXPECT_EQ(*table.find(std::string_view("b")), 2);
    EXPECT_EQ(*table.find(std::string_view("c")), 3);
    EXPECT_EQ(table.probe_length(std::string_view("c")), 3u);   // через надгробие
    // Вставка берёт место надгробия.
    table.insert_or_assign("d", 4);
    EXPECT_EQ(table.tombstones(), 0u);
}

TEST(OpenTable, ChurnDoesNotLetTombstonesFillTheTable) {
    // Ключи приходят и уходят — как ники в чате или задачи в очереди.
    // Порог считает надгробия, и рехеширование их вычищает: таблица не
    // зарастает ими и не растёт без конца при постоянном размере.
    Open table;
    for (int round = 0; round < 100000; ++round) {
        table.insert_or_assign(key_of(round), round);
        if (round >= 100) {
            table.erase(key_of(round - 100));
        }
    }
    EXPECT_EQ(table.size(), 100u);
    EXPECT_LE(table.bucket_count(), 512u);
    EXPECT_LT(static_cast<double>(table.size() + table.tombstones()),
              0.75 * static_cast<double>(table.bucket_count()) + 1);
}
