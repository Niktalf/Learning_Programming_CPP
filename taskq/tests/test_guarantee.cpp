// Сильная гарантия пакетной постановки. Рассуждения на словах
// доказательством не являются — нужен тип, который бросает в заданный момент.

#include <gtest/gtest.h>

#include <cstddef>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/task_store.hpp"

namespace {

// Сколько копирований осталось до броска; -1 — не бросать.
int& countdown() {
    static int n = -1;
    return n;
}

class ThrowOnNthCopy {
public:
    ThrowOnNthCopy() = default;
    explicit ThrowOnNthCopy(std::string text) : text_(std::move(text)) {}

    ThrowOnNthCopy(const ThrowOnNthCopy& other) : text_(other.text_) { tick(); }
    ThrowOnNthCopy& operator=(const ThrowOnNthCopy& other) {
        tick();
        text_ = other.text_;
        return *this;
    }
    // Перемещение не бросает — ровно как у настоящей Task.
    ThrowOnNthCopy(ThrowOnNthCopy&&) noexcept = default;
    ThrowOnNthCopy& operator=(ThrowOnNthCopy&&) noexcept = default;

    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    static void tick() {
        if (countdown() > 0 && --countdown() == 0) {
            throw std::bad_alloc();
        }
    }
    std::string text_;
};

using Store = taskq::BasicTaskStore<ThrowOnNthCopy>;
using Batch = std::vector<std::pair<std::string_view, ThrowOnNthCopy>>;

// Три очереди, в каждой по одной старой задаче.
Store make_store() {
    Store store;
    for (const char* const queue : {"a", "b", "c"}) {
        store.create_queue(queue);
        store.submit(queue, ThrowOnNthCopy("old"));
    }
    return store;
}

std::vector<std::size_t> sizes(const Store& store) {
    return {store.size("a"), store.size("b"), store.size("c")};
}

// Пакет из четырёх задач в три очереди; в «a» — две.
Batch make_batch() {
    Batch batch;
    batch.emplace_back("a", ThrowOnNthCopy("n1"));
    batch.emplace_back("b", ThrowOnNthCopy("n2"));
    batch.emplace_back("c", ThrowOnNthCopy("n3"));
    batch.emplace_back("a", ThrowOnNthCopy("n4"));
    return batch;
}

}  // namespace

TEST(Guarantee, BatchIsAllOrNothing) {
    // Успешный пакет делает семь копирований: три копии очередей по одной
    // старой задаче и четыре копии новых задач. Бросок перебирается на
    // каждом — и в копии очереди, и на первой задаче, и в середине пакета,
    // и на последней задаче. Одна точка броска доказывала бы гарантию только
    // для неё.
    const Batch batch = make_batch();
    constexpr int copies_in_successful_batch = 7;
    for (int nth = 1; nth <= copies_in_successful_batch; ++nth) {
        Store store = make_store();

        countdown() = nth;
        bool thrown = false;
        try {
            store.submit_batch(batch);
        } catch (const std::bad_alloc&) {
            thrown = true;
        }
        countdown() = -1;

        ASSERT_TRUE(thrown) << "copy " << nth;
        EXPECT_EQ(sizes(store), (std::vector<std::size_t>{1, 1, 1})) << "copy " << nth;
        EXPECT_EQ(store.peek("a")->text(), std::string("old")) << "copy " << nth;
    }

    // Восьмого копирования нет: перебор выше покрыл все точки броска.
    Store store = make_store();
    countdown() = copies_in_successful_batch + 1;
    EXPECT_NO_THROW(store.submit_batch(batch));
    countdown() = -1;
    EXPECT_EQ(sizes(store), (std::vector<std::size_t>{3, 2, 2}));
}

TEST(Guarantee, UnknownQueueInTheMiddleChangesNothing) {
    Store store = make_store();
    Batch batch;
    batch.emplace_back("a", ThrowOnNthCopy("n1"));
    batch.emplace_back("absent", ThrowOnNthCopy("n2"));
    batch.emplace_back("b", ThrowOnNthCopy("n3"));

    EXPECT_THROW(store.submit_batch(batch), taskq::UnknownQueueError);
    EXPECT_EQ(sizes(store), (std::vector<std::size_t>{1, 1, 1}));
}

TEST(Guarantee, BatchKeepsOrderWithinQueue) {
    Store store = make_store();
    store.submit_batch(make_batch());
    EXPECT_EQ(store.take("a")->text(), std::string("old"));
    EXPECT_EQ(store.take("a")->text(), std::string("n1"));
    EXPECT_EQ(store.take("a")->text(), std::string("n4"));
}

TEST(Guarantee, CommitIsNoexcept) {
    // Фиксация пакета — обмен векторов. Если бы swap мог бросить, последняя
    // операция перестала бы быть точкой невозврата: часть очередей обменялась
    // бы, часть нет, и сильная гарантия молча выродилась бы в базовую.
    taskq::Queue a;
    taskq::Queue b;
    EXPECT_TRUE(noexcept(a.swap(b)));
    // POLL переносит задачу в аренду перемещением и полагается на то, что
    // оно не бросает.
    EXPECT_TRUE(std::is_nothrow_move_assignable_v<taskq::Task>);
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<taskq::Task>);
}
