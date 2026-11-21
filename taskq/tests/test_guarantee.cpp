// Сильная гарантия пакетной постановки. Рассуждения на словах
// доказательством не являются — нужен тип, который бросает в заданный момент.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/payload.hpp"
#include "taskq/task.hpp"
#include "taskq/task_store.hpp"
// Этап 9: хранилище видит только объявление фабрики индекса. Для задачи
// Task она инстанцирована в ядре; для тестовых типов определение нужно
// здесь — поэтому тест включает реализации индекса.
#include "taskq/engines.hpp"

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

    ThrowOnNthCopy(const ThrowOnNthCopy& other) : text_(other.text_), id_(other.id_) { tick(); }
    ThrowOnNthCopy& operator=(const ThrowOnNthCopy& other) {
        tick();
        text_ = other.text_;
        id_ = other.id_;
        return *this;
    }
    // Перемещение не бросает — ровно как у настоящей Task.
    ThrowOnNthCopy(ThrowOnNthCopy&&) noexcept = default;
    ThrowOnNthCopy& operator=(ThrowOnNthCopy&&) noexcept = default;

    [[nodiscard]] const std::string& text() const noexcept { return text_; }

    // Этап 5: хранилище требует номер и ключ выборки — концепция QueuedTask.
    // Номера различны (этап 7: индекс по номеру), копия — с номером оригинала.
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] taskq::TaskKey key() const { return taskq::TaskKey{0, sequence_, std::string()}; }
    void set_sequence(std::uint64_t sequence) noexcept { sequence_ = sequence; }

private:
    static void tick() {
        if (countdown() > 0 && --countdown() == 0) {
            throw std::bad_alloc();
        }
    }
    std::string text_;
    std::uint64_t id_ = next_id();
    std::uint64_t sequence_ = 0;

    static std::uint64_t next_id() {
        static std::uint64_t id = 0;
        return ++id;
    }
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
    // Этап 7: пакет вставляется на место, и при отказе каждая вставка
    // откатывается. Перебираются все точки отказа внутри вставки: очередь
    // k-й задачи подменяется несуществующей, и к этому моменту уже вставлены
    // и проиндексированы k задач — от нуля до всех, кроме последней. Одна
    // точка отказа доказывала бы гарантию только для неё.
    for (std::size_t k = 0; k < 4; ++k) {
        Store store = make_store();
        Batch batch = make_batch();
        batch[k].first = "absent";
        EXPECT_THROW(store.submit_batch(std::move(batch)), taskq::UnknownQueueError) << "item " << k;
        EXPECT_EQ(sizes(store), (std::vector<std::size_t>{1, 1, 1})) << "item " << k;
        EXPECT_EQ(store.peek("a")->text(), std::string("old")) << "item " << k;
    }
    Store store = make_store();
    store.submit_batch(make_batch());
    EXPECT_EQ(sizes(store), (std::vector<std::size_t>{3, 2, 2}));
}

TEST(Guarantee, CopiesHappenBeforeTheStoreIsTouched) {
    // Пакет принимается по значению: кто хочет сохранить оригинал, отдаёт
    // копию, и все копирования — до первого изменения хранилища. До этапа 7
    // копии делались внутри submit_batch, и бросок проверялся на каждой;
    // теперь бросок копии вообще не доходит до хранилища.
    const Batch batch = make_batch();
    for (int nth = 1; nth <= 4; ++nth) {
        Store store = make_store();
        countdown() = nth;
        EXPECT_THROW(store.submit_batch(batch), std::bad_alloc) << "copy " << nth;
        countdown() = -1;
        EXPECT_EQ(sizes(store), (std::vector<std::size_t>{1, 1, 1})) << "copy " << nth;
    }
}

TEST(Guarantee, DuplicateIdRollsBackTheTreeNodeToo) {
    // Отказ при вставке в индекс — второй путь отката: задача уже в дереве
    // очереди, а в индекс не попала. Откат удаляет и узел.
    Store store = make_store();
    Batch batch = make_batch();
    batch[3].second = batch[1].second;   // тот же номер дважды
    EXPECT_THROW(store.submit_batch(std::move(batch)), std::logic_error);
    EXPECT_EQ(sizes(store), (std::vector<std::size_t>{1, 1, 1}));
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

TEST(Guarantee, RollbackLeavesNoTraceInTheIndex) {
    // Этап 7: откат вставок обязан убрать и записи индекса по номеру — иначе
    // STATUS нашёл бы задачу, которой в очереди нет. Отказ — на последней
    // задаче, когда три уже вставлены и проиндексированы.
    Store store = make_store();
    const Batch batch = make_batch();
    Batch failing = batch;
    failing.back().first = "absent";
    EXPECT_THROW(store.submit_batch(std::move(failing)), taskq::UnknownQueueError);
    for (const auto& [queue, task] : batch) {
        EXPECT_FALSE(store.find(task.id()).has_value()) << task.text();
    }
    EXPECT_EQ(sizes(store), (std::vector<std::size_t>{1, 1, 1}));
}

TEST(Guarantee, MovesThatMustNotThrowDoNot) {
    // POLL переносит задачу в аренду перемещением и полагается на то, что
    // оно не бросает. Откат пакета опирается на удаление узла дерева и
    // записи индекса по итератору и по числовому ключу: стандарт гарантирует,
    // что они не бросают, хотя noexcept на них и не написан — проверить это
    // можно только чтением стандарта, а не static_assert.
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

    // Три копии новых задач — при передаче пакета по значению, до первого
    // изменения хранилища. Копий очередей с этапа 7 нет.
    constexpr int clones_in_successful_batch = 3;
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
