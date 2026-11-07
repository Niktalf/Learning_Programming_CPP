// Сильная гарантия пакетной постановки. Рассуждения на словах
// доказательством не являются — нужен тип, который бросает в заданный момент.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/payload.hpp"
#include "taskq/task.hpp"
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

    // Этап 5: хранилище требует номер и ключ выборки — концепция QueuedTask.
    [[nodiscard]] std::uint64_t id() const noexcept { return 0; }
    [[nodiscard]] taskq::TaskKey key() const { return taskq::TaskKey{0, sequence_, std::string()}; }
    void set_sequence(std::uint64_t sequence) noexcept { sequence_ = sequence; }

private:
    static void tick() {
        if (countdown() > 0 && --countdown() == 0) {
            throw std::bad_alloc();
        }
    }
    std::string text_;
    std::uint64_t sequence_ = 0;
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

// --- Этап 4: та же гарантия на настоящих задачах с полиморфным телом -------
//
// После перехода на unique_ptr копирование задачи держится на clone тела.
// Тест подставляет тело, чей clone бросает на N-м вызове, и повторяет перебор
// точек броска уже на настоящем TaskStore.

namespace {

int& clone_countdown() {
    static int n = -1;
    return n;
}

class ThrowingClone final : public taskq::IPayload {
public:
    explicit ThrowingClone(std::string text) : text_(std::move(text)) {}
    ThrowingClone(const ThrowingClone&) = default;
    ThrowingClone& operator=(const ThrowingClone&) = delete;

    [[nodiscard]] std::string_view type_name() const noexcept override { return "throwing"; }
    [[nodiscard]] std::string to_string() const override { return text_; }
    [[nodiscard]] std::size_t size() const noexcept override { return text_.size(); }
    [[nodiscard]] taskq::PayloadPtr clone() const override {
        if (clone_countdown() > 0 && --clone_countdown() == 0) {
            throw std::bad_alloc();
        }
        return std::make_unique<ThrowingClone>(*this);
    }
    [[nodiscard]] bool equals(const taskq::IPayload& other) const noexcept override {
        const auto* const same = dynamic_cast<const ThrowingClone*>(&other);
        return same != nullptr && same->text_ == text_;
    }

private:
    std::string text_;
};

taskq::Task throwing_task(taskq::TaskId id, std::string text) {
    return taskq::Task(id, std::make_unique<ThrowingClone>(std::move(text)));
}

}  // namespace

TEST(Guarantee, BatchWithPolymorphicPayloads) {
    std::vector<std::pair<std::string_view, taskq::Task>> batch;
    batch.emplace_back("a", throwing_task(11, "n1"));
    batch.emplace_back("b", throwing_task(12, "n2"));
    batch.emplace_back("a", throwing_task(13, "n3"));

    // Две копии очередей по одной старой задаче и три копии новых.
    constexpr int clones_in_successful_batch = 5;
    for (int nth = 1; nth <= clones_in_successful_batch; ++nth) {
        taskq::TaskStore store;
        store.create_queue("a");
        store.create_queue("b");
        store.submit("a", throwing_task(1, "old"));
        store.submit("b", throwing_task(2, "old"));

        clone_countdown() = nth;
        EXPECT_THROW(store.submit_batch(batch), std::bad_alloc) << "clone " << nth;
        clone_countdown() = -1;

        EXPECT_EQ(store.size("a"), 1u) << "clone " << nth;
        EXPECT_EQ(store.size("b"), 1u) << "clone " << nth;
        EXPECT_EQ(store.peek("a")->to_string(), std::string("old")) << "clone " << nth;
    }
}
