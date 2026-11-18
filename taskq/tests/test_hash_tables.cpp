// Этап 8: собственные хеш-таблицы с номером задачи в роли ключа.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "taskq/chained_table.hpp"
#include "taskq/hash.hpp"
#include "taskq/open_table.hpp"

using taskq::ChainedTable;
using taskq::Fnv1aHash;
using taskq::ModuloHash;
using taskq::OpenTable;

namespace {

// Все номера сталкиваются: проверка разрешения коллизий, а не удачи хеша.
struct ConstantHash {
    [[nodiscard]] std::size_t operator()(std::uint64_t) const noexcept { return 3; }
};

}  // namespace

TEST(Hash, FnvMixesEveryByte) {
    EXPECT_NE(taskq::fnv1a(1), taskq::fnv1a(2));
    EXPECT_NE(taskq::fnv1a(1), taskq::fnv1a(1ULL << 56));   // старший байт тоже влияет
    EXPECT_NE(taskq::fnv1a(7, 1), taskq::fnv1a(7, 2));       // затравка меняет хеш
    EXPECT_EQ(ModuloHash{}(5), ModuloHash{}(21));
}

TEST(ChainedTable, SequentialIdsChurn) {
    // Скользящее окно, как в очереди: выдаётся старая задача, ставится новая.
    ChainedTable<std::uint64_t, int, Fnv1aHash> table;
    for (std::uint64_t id = 1; id <= 1000; ++id) {
        table.insert_new(id, static_cast<int>(id));
    }
    for (std::uint64_t id = 1; id <= 20000; ++id) {
        ASSERT_TRUE(table.erase(id));
        table.insert_new(id + 1000, 0);
    }
    EXPECT_EQ(table.size(), 1000u);
    EXPECT_EQ(table.find(std::uint64_t{20000}), nullptr);
    EXPECT_NE(table.find(std::uint64_t{20001}), nullptr);
    EXPECT_NE(table.find(std::uint64_t{21000}), nullptr);
}

TEST(ChainedTable, ReserveAvoidsRehash) {
    ChainedTable<std::uint64_t, int, Fnv1aHash> table;
    table.reserve(1000);
    const std::size_t buckets = table.bucket_count();
    int* const first = &table.insert_new(1, 1);
    for (std::uint64_t id = 2; id <= 1000; ++id) {
        table.insert_new(id, 0);
    }
    EXPECT_EQ(table.bucket_count(), buckets);
    EXPECT_EQ(first, table.find(std::uint64_t{1}));
}

TEST(OpenTable, ErasedCellBecomesTombstone) {
    // Почему нельзя просто освободить ячейку. Задачи 5 и 17 с одной
    // домашней ячейкой: 5 легла в неё, 17 — в следующую. Если выдача 5
    // сделает ячейку пустой, поиск 17 остановится на ней, и ACK 17 ответит
    // «нет такой задачи».
    OpenTable<std::uint64_t, int, ConstantHash> table;
    table.insert_new(5, 5);
    table.insert_new(17, 17);
    ASSERT_TRUE(table.erase(std::uint64_t{5}));
    EXPECT_EQ(table.tombstones(), 1u);
    ASSERT_NE(table.find(std::uint64_t{17}), nullptr);
    EXPECT_EQ(*table.find(std::uint64_t{17}), 17);
    table.insert_new(29, 29);   // занимает надгробие
    EXPECT_EQ(table.tombstones(), 0u);
    EXPECT_EQ(table.probe_length(std::uint64_t{29}), 1u);
}

TEST(OpenTable, MillionsPassTenStay) {
    // Индекс, через который прошло много задач при немногих живых: порог
    // считает надгробия, и перестройка их убирает — таблица не растёт и не
    // зарастает.
    OpenTable<std::uint64_t, int, Fnv1aHash> table;
    for (std::uint64_t id = 1; id <= 10; ++id) {
        table.insert_new(id, 0);
    }
    for (std::uint64_t id = 1; id <= 100000; ++id) {
        ASSERT_TRUE(table.erase(id));
        table.insert_new(id + 10, 0);
    }
    EXPECT_EQ(table.size(), 10u);
    EXPECT_LE(table.bucket_count(), 32u);
    EXPECT_EQ(table.find(std::uint64_t{99999}), nullptr);
}

TEST(BadHash, SixteenBucketsAtMost) {
    ChainedTable<std::uint64_t, int, ModuloHash> table;
    for (std::uint64_t id = 1; id <= 4096; ++id) {
        table.insert_new(id, 0);
    }
    std::size_t busy = 0;
    for (std::size_t bucket = 0; bucket < table.bucket_count(); ++bucket) {
        busy += table.chain_length(bucket) > 0 ? 1 : 0;
    }
    EXPECT_GE(table.bucket_count(), 4096u);
    EXPECT_EQ(busy, 16u);   // 4096 корзин, занято 16, в каждой по 256 задач
    EXPECT_NE(table.find(std::uint64_t{4000}), nullptr);
}
