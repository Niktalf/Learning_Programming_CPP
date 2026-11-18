// Этап 8: собственные хеш-таблицы сами по себе, без хранилища чата.

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "chat/chained_table.hpp"
#include "chat/hash.hpp"
#include "chat/open_table.hpp"

using chat::ChainedTable;
using chat::CharSumHash;
using chat::Fnv1aHash;
using chat::OpenTable;

namespace {

// Хеш, у которого все ключи сталкиваются: проверяет разрешение коллизий,
// а не удачу хеш-функции.
struct ConstantHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view) const noexcept { return 7; }
};

}  // namespace

TEST(Hash, FnvSeparatesAnagramsCharSumDoesNot) {
    EXPECT_NE(chat::fnv1a("alice"), chat::fnv1a("ecila"));
    EXPECT_EQ(chat::char_sum("alice"), chat::char_sum("ecila"));
    // Затравка меняет хеш: подобранные без неё коллизии с ней не работают.
    EXPECT_NE(chat::fnv1a("alice", 1), chat::fnv1a("alice", 2));
}

TEST(ChainedTable, InsertFindErase) {
    ChainedTable<std::string, int, Fnv1aHash> table;
    for (int i = 0; i < 1000; ++i) {
        table.insert_new("nick" + std::to_string(i), i);
    }
    EXPECT_EQ(table.size(), 1000u);
    EXPECT_LE(table.load_factor(), 1.0);   // порог держится ростом
    EXPECT_EQ(*table.find(std::string_view("nick500")), 500);
    EXPECT_TRUE(table.erase(std::string_view("nick500")));
    EXPECT_FALSE(table.erase(std::string_view("nick500")));
    EXPECT_EQ(table.find(std::string_view("nick500")), nullptr);
    EXPECT_EQ(table.size(), 999u);
}

TEST(ChainedTable, ReferencesSurviveRehash) {
    // Узлы перевешиваются, а не переезжают: комната, найденная до роста
    // таблицы, остаётся на месте — как у std::unordered_map.
    ChainedTable<std::string, int, Fnv1aHash> table;
    int& first = table.insert_new("first", 1);
    const std::size_t buckets = table.bucket_count();
    for (int i = 0; i < 100; ++i) {
        table.insert_new("n" + std::to_string(i), i);
    }
    ASSERT_GT(table.bucket_count(), buckets);
    EXPECT_EQ(&first, table.find(std::string_view("first")));
}

TEST(ChainedTable, CollisionsResolveCorrectly) {
    ChainedTable<std::string, int, ConstantHash> table;
    table.insert_new("a", 1);
    table.insert_new("b", 2);
    table.insert_new("c", 3);
    EXPECT_EQ(table.chain_length(7 & (table.bucket_count() - 1)), 3u);
    EXPECT_TRUE(table.erase(std::string_view("b")));   // из середины цепочки
    EXPECT_EQ(*table.find(std::string_view("a")), 1);
    EXPECT_EQ(*table.find(std::string_view("c")), 3);
}

TEST(ChainedTable, LongChainIsDestroyedWithoutRecursion) {
    // Двести тысяч узлов в одной цепочке: рекурсивное разрушение
    // unique_ptr-списка такой длины переполнило бы стек. insert_new не ищет
    // ключ (предусловие — его нет), поэтому вставка остаётся быстрой.
    auto table = std::make_unique<ChainedTable<std::string, int, ConstantHash>>(ConstantHash{}, 1e9);
    for (int i = 0; i < 200000; ++i) {
        table->insert_new(std::to_string(i), i);
    }
    EXPECT_EQ(table->size(), 200000u);
    table.reset();   // не должно упасть
}

TEST(ChainedTable, MovableValues) {
    // Комната владеет сообщениями в unique_ptr: таблица обязана принимать
    // некопируемые значения.
    ChainedTable<std::string, std::unique_ptr<int>, Fnv1aHash> table;
    table.insert_new("x", std::make_unique<int>(5));
    EXPECT_EQ(**table.find(std::string_view("x")), 5);
}

TEST(OpenTable, ErasedCellBecomesTombstone) {
    // Почему нельзя просто освободить ячейку. У всех ключей одна домашняя
    // ячейка: a лёг в неё, b — в следующую. Если удаление a сделает ячейку
    // пустой, поиск b остановится на ней и не найдёт b.
    OpenTable<std::string, int, ConstantHash> table;
    table.insert_new("a", 1);
    table.insert_new("b", 2);
    EXPECT_TRUE(table.erase(std::string_view("a")));
    EXPECT_EQ(table.tombstones(), 1u);
    ASSERT_NE(table.find(std::string_view("b")), nullptr);   // пробег прошёл через надгробие
    EXPECT_EQ(*table.find(std::string_view("b")), 2);
    EXPECT_EQ(table.probe_length(std::string_view("b")), 2u);
    // Вставка занимает надгробие, а не новую ячейку.
    table.insert_new("c", 3);
    EXPECT_EQ(table.tombstones(), 0u);
    EXPECT_EQ(table.probe_length(std::string_view("c")), 1u);
}

TEST(OpenTable, ChurnDoesNotAccumulateTombstones) {
    // Реестр ников: входят и выходят постоянно, число живых постоянно.
    // Порог считает надгробия, и рехеширование их убирает — иначе таблица
    // заросла бы ими целиком, и поиск отсутствующего ника не останавливался
    // бы никогда.
    OpenTable<std::string, int, Fnv1aHash> table;
    for (int i = 0; i < 100; ++i) {
        table.insert_new("user" + std::to_string(i), i);
    }
    const auto churn = [&table](int from, int to) {
        for (int i = from; i < to; ++i) {
            ASSERT_TRUE(table.erase(std::string_view("user" + std::to_string(i))));
            table.insert_new("user" + std::to_string(i + 100), i);
        }
    };
    // Первая перестройка может удвоить таблицу — живых больше половины
    // порога, — но дальше размер устанавливается: надгробия убираются
    // перестройкой того же размера.
    churn(0, 2000);
    const std::size_t cells = table.bucket_count();
    churn(2000, 22000);
    EXPECT_EQ(table.size(), 100u);
    EXPECT_EQ(table.bucket_count(), cells);   // перестраивалась, но не росла
    EXPECT_LE(static_cast<double>(table.size() + table.tombstones()),
              0.75 * static_cast<double>(table.bucket_count()));
    EXPECT_EQ(table.find(std::string_view("nobody")), nullptr);
}

TEST(OpenTable, GrowsAndKeepsEveryKey) {
    OpenTable<std::string, int, Fnv1aHash> table;
    for (int i = 0; i < 5000; ++i) {
        table.insert_new("k" + std::to_string(i), i);
    }
    EXPECT_LE(table.load_factor(), 0.75);
    for (int i = 0; i < 5000; ++i) {
        ASSERT_NE(table.find(std::string_view("k" + std::to_string(i))), nullptr) << i;
    }
}

TEST(BadHash, CorrectButCrowded) {
    // Плохой хеш не ломает корректность — только скорость. Ники одной
    // длины из одних букв сгрудятся в немногих корзинах.
    ChainedTable<std::string, int, CharSumHash> table;
    for (char a = 'a'; a <= 'z'; ++a) {
        for (char b = 'a'; b <= 'z'; ++b) {
            table.insert_new(std::string{a, b}, 0);
        }
    }
    EXPECT_EQ(table.size(), 676u);
    std::size_t busy = 0;
    for (std::size_t bucket = 0; bucket < table.bucket_count(); ++bucket) {
        busy += table.chain_length(bucket) > 0 ? 1 : 0;
    }
    EXPECT_EQ(busy, 51u);   // сумма двух букв — от 194 до 244: 51 значение на 676 ников
    EXPECT_NE(table.find(std::string_view("zz")), nullptr);
}
