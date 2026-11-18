// Этап 7: кольцевой буфер истории и его итератор.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <ranges>
#include <string>
#include <vector>

#include "chat/ring_buffer.hpp"

using chat::RingBuffer;
using Ring = RingBuffer<int>;

// Главная проверка этапа — при компиляции. Двунаправленный — значит, и
// прямой; двунаправленность нужна views::reverse в истории.
static_assert(std::forward_iterator<Ring::iterator>);
static_assert(std::forward_iterator<Ring::const_iterator>);
static_assert(std::bidirectional_iterator<Ring::const_iterator>);
static_assert(!std::random_access_iterator<Ring::const_iterator>);
static_assert(std::ranges::bidirectional_range<const Ring>);
static_assert(std::ranges::common_range<const Ring>);
static_assert(std::is_convertible_v<Ring::iterator, Ring::const_iterator>);
static_assert(!std::is_convertible_v<Ring::const_iterator, Ring::iterator>);

// Звёздочка: from_oldest() — тот же итератор, но конец — страж.
using FromOldest = decltype(std::declval<const Ring&>().from_oldest());
static_assert(std::sentinel_for<Ring::End, Ring::const_iterator>);
static_assert(std::ranges::bidirectional_range<FromOldest>);
static_assert(!std::ranges::common_range<FromOldest>);

namespace {

Ring filled(std::size_t capacity, int count) {
    Ring ring(capacity);
    for (int i = 0; i < count; ++i) {
        ring.push_back(i);
    }
    return ring;
}

std::vector<int> items_of(const Ring& ring) {
    return std::vector<int>(ring.begin(), ring.end());
}

}  // namespace

TEST(RingBuffer, KeepsTheNewestAndEvictsTheOldest) {
    const Ring ring = filled(4, 10);
    EXPECT_EQ(ring.size(), 4u);
    EXPECT_TRUE(ring.full());
    EXPECT_EQ(ring.evicted(), 6u);
    EXPECT_EQ(items_of(ring), (std::vector<int>{6, 7, 8, 9}));
    EXPECT_EQ(ring.front(), 6);
    EXPECT_EQ(ring.back(), 9);
}

TEST(RingBuffer, IterationCrossesTheWrapPoint) {
    // Пять добавлений в кольцо на четыре: физически самое старое — во втором
    // слоте, и обход обязан пройти через конец вектора к его началу.
    for (int count = 0; count <= 13; ++count) {
        const Ring ring = filled(4, count);
        std::vector<int> expected;
        for (int i = std::max(0, count - 4); i < count; ++i) {
            expected.push_back(i);
        }
        EXPECT_EQ(items_of(ring), expected) << count;
    }
}

TEST(RingBuffer, RangeBasedForAndAlgorithms) {
    const Ring ring = filled(5, 12);   // 7 8 9 10 11
    int sum = 0;
    for (const int item : ring) {
        sum += item;
    }
    EXPECT_EQ(sum, 45);
    EXPECT_EQ(std::accumulate(ring.begin(), ring.end(), 0), 45);
    EXPECT_EQ(std::count_if(ring.begin(), ring.end(), [](int item) { return item % 2 == 0; }), 2);
    EXPECT_EQ(*std::find(ring.begin(), ring.end(), 10), 10);
    EXPECT_EQ(std::distance(ring.begin(), ring.end()), 5);
    EXPECT_TRUE(std::ranges::is_sorted(ring));
}

TEST(RingBuffer, ReverseAndPipelines) {
    // Двунаправленность — ради этого: последние сообщения читаются с конца.
    const Ring ring = filled(4, 6);   // 2 3 4 5
    const auto reversed = ring | std::views::reverse;
    EXPECT_EQ(std::vector<int>(reversed.begin(), reversed.end()), (std::vector<int>{5, 4, 3, 2}));
    auto last_two = ring | std::views::reverse | std::views::take(2);
    EXPECT_EQ(std::ranges::fold_left(last_two, 0, std::plus<>{}), 9);
    // enumerate | reverse — конвейер истории этапа 5 — на кольце.
    std::vector<std::size_t> positions;
    for (const auto& [position, item] : ring | std::views::enumerate | std::views::reverse) {
        positions.push_back(static_cast<std::size_t>(position));
        static_cast<void>(item);
    }
    EXPECT_EQ(positions, (std::vector<std::size_t>{3, 2, 1, 0}));
}

TEST(RingBuffer, MutableIteratorChangesElements) {
    Ring ring = filled(3, 5);   // 2 3 4
    for (int& item : ring) {
        item *= 10;
    }
    EXPECT_EQ(items_of(ring), (std::vector<int>{20, 30, 40}));
}

TEST(RingBuffer, SentinelRangeWorksWithRangesAlgorithms) {
    // Звёздочка: алгоритмы ranges и range-based for принимают страж;
    // классическим std::accumulate нужна пара одного типа, и с from_oldest
    // он бы не собрался.
    const Ring ring = filled(4, 7);   // 3 4 5 6
    int sum = 0;
    for (const int item : ring.from_oldest()) {
        sum += item;
    }
    EXPECT_EQ(sum, 18);
    EXPECT_EQ(std::ranges::count_if(ring.from_oldest(), [](int item) { return item > 4; }), 2);
    EXPECT_EQ(std::ranges::distance(ring.from_oldest()), 4);
    EXPECT_FALSE(ring.begin() == Ring::End{});
    EXPECT_TRUE(Ring(2).begin() == Ring::End{});   // пустой: начало — уже конец
}

TEST(RingBuffer, ReserveOneMoreMakesTheNextPushAllocationFree) {
    Ring ring(1000);
    for (int i = 0; i < 100; ++i) {
        ring.reserve_one_more();
        // front() пустого буфера — неопределённое поведение, как у vector:
        // адрес берётся, только если есть что брать.
        const int* const first = ring.empty() ? nullptr : &ring.front();
        ring.push_back(i);
        // Если элементы были, запас не дал им переехать.
        if (first != nullptr) {
            EXPECT_EQ(first, &ring.front()) << i;
        }
    }
}

// --- Инвалидация (домашнее задание этапа 7) ------------------------------
//
// Итератор кольца — буфер и логический индекс, а не указатель на элемент.
// Поэтому правила у него свои, и все они — определённое поведение, которое
// можно проверить тестом.

TEST(RingBuffer, IteratorsSurviveGrowthButReferencesDoNot) {
    // Пока буфер не полон, он растёт как vector: перевыделение переносит
    // элементы. Итератор помнит индекс — он по-прежнему указывает на тот же
    // элемент. Адрес элемента — нет.
    // Адрес сравнивается как число: указатель на освобождённую память нельзя
    // даже разыменовать, а сравнивать его самого — сомнительно.
    Ring ring(1000);
    ring.push_back(7);
    const Ring::const_iterator it = ring.begin();
    const auto address = reinterpret_cast<std::uintptr_t>(&ring.front());
    for (int i = 0; i < 100; ++i) {
        ring.push_back(i);
    }
    EXPECT_EQ(*it, 7);
    EXPECT_NE(reinterpret_cast<std::uintptr_t>(&ring.front()), address);
}

TEST(RingBuffer, EvictionShiftsWhatAnIteratorSees) {
    // Полный буфер: push_back вытесняет самое старое, и тот же логический
    // индекс теперь означает следующее сообщение. Итератор не висит — он
    // смотрит на другой элемент. Для истории это значит: обход нельзя
    // смешивать с добавлением, как и у любого контейнера.
    Ring ring = filled(3, 3);   // 0 1 2
    const Ring::const_iterator oldest = ring.begin();
    EXPECT_EQ(*oldest, 0);
    ring.push_back(3);          // 1 2 3
    EXPECT_EQ(*oldest, 1);
    // Конец остаётся концом: размер полного буфера не меняется.
    const Ring::const_iterator end = ring.end();
    ring.push_back(4);
    EXPECT_EQ(end, ring.end());
}

TEST(RingBuffer, EndStopsBeingEndWhenNotFull) {
    // Пока буфер не полон, push_back сдвигает конец: старый end() теперь
    // указывает на новый элемент.
    Ring ring = filled(5, 2);
    const Ring::const_iterator old_end = ring.end();
    ring.push_back(42);
    EXPECT_NE(old_end, ring.end());
    EXPECT_EQ(*old_end, 42);
}
