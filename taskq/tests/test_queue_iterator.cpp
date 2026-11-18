// Этап 7: приоритетная выборка и собственный итератор очереди.

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

#include "taskq/handler.hpp"
#include "taskq/service.hpp"
#include "taskq/task_store.hpp"

using taskq::Task;
using taskq::TaskStore;
using QueueIterator = TaskStore::QueueIterator;

// Главная проверка этапа — при компиляции.
static_assert(std::forward_iterator<QueueIterator>);
static_assert(std::ranges::forward_range<TaskStore::QueueView>);
static_assert(std::is_same_v<std::iter_reference_t<QueueIterator>, const Task&>);
// Обход только смотрит: изменить задачу через итератор нельзя.
static_assert(!std::is_assignable_v<std::iter_reference_t<QueueIterator>, Task>);

// Звёздочка: until_end() — тот же итератор, конец — страж.
using UntilEnd = decltype(std::declval<const TaskStore::QueueView&>().until_end());
static_assert(std::sentinel_for<TaskStore::QueueEnd, QueueIterator>);
static_assert(std::ranges::forward_range<UntilEnd>);
static_assert(!std::ranges::common_range<UntilEnd>);

namespace {

// Задачи с номерами 1..n и приоритетами из списка, в порядке постановки.
TaskStore with_priorities(const std::vector<int>& priorities) {
    TaskStore store;
    store.create_queue("build");
    taskq::TaskId id = 0;
    for (const int priority : priorities) {
        ++id;
        store.submit("build", Task(id, "t" + std::to_string(id), priority));
    }
    return store;
}

std::vector<taskq::TaskId> ids_of(const TaskStore::QueueView& queue) {
    std::vector<taskq::TaskId> ids;
    for (const Task& task : queue) {
        ids.push_back(task.id());
    }
    return ids;
}

}  // namespace

TEST(PriorityQueue, PollTakesHighestPriorityThenEarliest) {
    // Смешанные приоритеты с повторами: ошибку в порядке видно только на них.
    taskq::Service service;
    static_cast<void>(taskq::handle(service, "CREATE build"));
    for (const char* const line : {"SUBMIT build prio=1 a", "SUBMIT build prio=5 b", "SUBMIT build c",
                                   "SUBMIT build prio=5 d", "SUBMIT build prio=9 e", "SUBMIT build prio=1 f"}) {
        static_cast<void>(taskq::handle(service, line));
    }
    std::vector<std::string> order;
    for (int i = 0; i < 6; ++i) {
        order.push_back(taskq::handle(service, "POLL build").text);
    }
    EXPECT_EQ(order, (std::vector<std::string>{"id=5 e", "id=2 b", "id=4 d", "id=1 a", "id=6 f", "id=3 c"}));
    EXPECT_EQ(taskq::handle(service, "POLL build").text, std::string("(empty)"));
}

TEST(PriorityQueue, ReleasedTaskGoesBehindItsPeers) {
    // RELEASE ставит задачу заново: тот же приоритет, новый номер постановки
    // — за всеми задачами того же приоритета, но перед задачами ниже.
    taskq::Service service;
    for (const char* const line : {"CREATE q", "SUBMIT q prio=5 a", "SUBMIT q prio=5 b", "SUBMIT q c"}) {
        static_cast<void>(taskq::handle(service, line));
    }
    EXPECT_EQ(taskq::handle(service, "POLL q").text, std::string("id=1 a"));
    static_cast<void>(taskq::handle(service, "RELEASE 1"));
    EXPECT_EQ(taskq::handle(service, "LIST q").text,
              std::string("id=2 prio=5 b\nid=1 prio=5 a\nid=3 prio=0 c"));
}

TEST(QueueIterator, WalksInSelectionOrder) {
    const TaskStore store = with_priorities({0, 9, 5, 9, 0});
    EXPECT_EQ(ids_of(store.view("build")), (std::vector<taskq::TaskId>{2, 4, 3, 1, 5}));
}

TEST(QueueIterator, WalkingDoesNotChangeTheQueue) {
    TaskStore store = with_priorities({3, 7, 1});
    for (int pass = 0; pass < 3; ++pass) {
        EXPECT_EQ(ids_of(store.view("build")), (std::vector<taskq::TaskId>{2, 1, 3})) << pass;
    }
    EXPECT_EQ(store.size("build"), 3u);
    // А POLL — меняет: то, что обход показал первым, и уходит.
    EXPECT_EQ(store.take("build")->id(), 2u);
    EXPECT_EQ(ids_of(store.view("build")), (std::vector<taskq::TaskId>{1, 3}));
}

TEST(QueueIterator, WorksWithStandardAlgorithms) {
    const TaskStore store = with_priorities({2, 8, 8, 4});
    const auto queue = store.view("build");
    EXPECT_EQ(std::distance(queue.begin(), queue.end()), 4);
    EXPECT_EQ(std::count_if(queue.begin(), queue.end(), [](const Task& task) { return task.priority() == 8; }), 2);
    EXPECT_EQ(std::find_if(queue.begin(), queue.end(), [](const Task& task) { return task.priority() < 5; })->id(),
              4u);
    EXPECT_TRUE(std::ranges::is_sorted(queue, std::ranges::greater{}, &Task::priority));
    EXPECT_EQ(std::ranges::max_element(queue, {}, &Task::priority)->id(), 2u);
}

TEST(QueueIterator, IsMultiPass) {
    const TaskStore store = with_priorities({1, 2, 3});
    const auto queue = store.view("build");
    QueueIterator first = queue.begin();
    const QueueIterator copy = first;
    ++first;
    EXPECT_EQ(copy->id(), 3u);
    EXPECT_EQ(first->id(), 2u);
    EXPECT_EQ(std::next(copy), first);
}

TEST(QueueIterator, MissingQueueIsAnErrorNotAnEmptyQueue) {
    // Обход несуществующей очереди её не создаёт — пункт 6 этапа.
    const TaskStore store;
    EXPECT_THROW(static_cast<void>(store.view("absent")), taskq::UnknownQueueError);
    EXPECT_EQ(store.queue_count(), 0u);
}

TEST(QueueIterator, SentinelRangeWorksWithRangesAlgorithms) {
    // Звёздочка: until_end() — диапазон со стражем; алгоритмы ranges и
    // range-based for его принимают.
    const TaskStore store = with_priorities({5, 1, 9});
    std::vector<taskq::TaskId> ids;
    for (const Task& task : store.view("build").until_end()) {
        ids.push_back(task.id());
    }
    EXPECT_EQ(ids, (std::vector<taskq::TaskId>{3, 1, 2}));
    EXPECT_EQ(std::ranges::count_if(store.view("build").until_end(),
                                    [](const Task& task) { return task.priority() > 1; }),
              2);
    EXPECT_TRUE(with_priorities({}).view("build").begin() == TaskStore::QueueEnd{});
}

TEST(TaskIndex, FindsByIdInAnyQueue) {
    // Этап 7: индекс по номеру — STATUS, TYPE, BUMP без перебора очередей.
    TaskStore store;
    store.create_queue("a");
    store.create_queue("b");
    store.submit("a", Task(10, "x"));
    store.submit("b", Task(20, "y"));
    EXPECT_EQ(store.find(20)->get().to_string(), std::string("y"));
    EXPECT_EQ(store.queue_of(10), std::optional<std::string>("a"));
    static_cast<void>(store.take("a"));
    EXPECT_FALSE(store.find(10).has_value());
    EXPECT_FALSE(store.queue_of(10).has_value());
}
