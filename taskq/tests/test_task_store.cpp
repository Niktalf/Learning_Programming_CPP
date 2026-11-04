#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/task_store.hpp"
#include "tracked.hpp"

using taskq::BasicTaskStore;
using taskq::Task;
using taskq::TaskStore;
using taskq::testing::Tracked;

TEST(TaskStore, SubmitThenPeek) {
    TaskStore store;
    store.create_queue("build");
    store.submit("build", Task(1, "make all", 5));

    // if с инициализацией: результат ограничен областью проверки.
    if (const auto front = store.peek("build"); front) {
        EXPECT_EQ(front->id(), 1u);
    } else {
        ADD_FAILURE() << "queue must have a task";
    }
    EXPECT_EQ(store.size("build"), 1u);
}

TEST(TaskStore, TakeRemovesTaskInFifoOrder) {
    TaskStore store;
    store.create_queue("build");
    store.submit("build", Task(1, "first", 0));
    store.submit("build", Task(2, "second", 0));

    const auto first = store.take("build");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->id(), 1u);
    EXPECT_EQ(store.size("build"), 1u);

    const auto second = store.take("build");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->id(), 2u);
    EXPECT_FALSE(store.take("build").has_value());
}

TEST(TaskStore, EmptyQueueAndMissingQueueAreDifferent) {
    TaskStore store;
    store.create_queue("build");

    // Этап 2 сливал эти два случая в пустой optional. Теперь пустая очередь —
    // пустой optional, штатный ответ, а отсутствующая — исключение.
    EXPECT_FALSE(store.peek("build").has_value());
    EXPECT_THROW(static_cast<void>(store.peek("absent")), taskq::UnknownQueueError);
    EXPECT_THROW(static_cast<void>(store.take("absent")), taskq::UnknownQueueError);
    EXPECT_EQ(store.queue_count(), 1u);
}

TEST(TaskStore, SubmitToMissingQueueThrows) {
    TaskStore store;
    EXPECT_THROW(store.submit("absent", Task(1, "x")), taskq::UnknownQueueError);
    EXPECT_EQ(store.queue_count(), 0u);
}

TEST(TaskStore, PeekingMissingQueueCreatesNothing) {
    TaskStore store;
    EXPECT_THROW(static_cast<void>(store.peek("absent")), taskq::UnknownQueueError);
    // С operator[] на map здесь появилась бы пустая очередь, в которую
    // потом никто не заглянет.
    EXPECT_EQ(store.queue_count(), 0u);
    EXPECT_TRUE(store.empty());
}

TEST(TaskStore, CreateQueueReportsWhetherItWasNew) {
    TaskStore store;
    EXPECT_TRUE(store.create_queue("build"));
    EXPECT_FALSE(store.create_queue("build"));
    EXPECT_EQ(store.queue_count(), 1u);
}

TEST(TaskStore, SubmittingTemporaryDoesNotCopy) {
    BasicTaskStore<Tracked> store;
    store.create_queue("build");
    Tracked::reset();

    store.submit("build", Tracked{std::string("payload")});

    EXPECT_EQ(Tracked::copies(), 0);
    EXPECT_GE(Tracked::moves(), 1);
}

TEST(TaskStore, SubmittingNamedTaskCopiesExactlyOnce) {
    BasicTaskStore<Tracked> store;
    store.create_queue("build");
    Tracked named{std::string("payload")};
    Tracked::reset();

    store.submit("build", named);   // копия в параметр, дальше перемещение

    EXPECT_EQ(Tracked::copies(), 1);
}

TEST(TaskStore, ExplicitMoveDoesNotCopy) {
    BasicTaskStore<Tracked> store;
    store.create_queue("build");
    Tracked named{std::string("payload")};
    Tracked::reset();

    store.submit("build", std::move(named));

    EXPECT_EQ(Tracked::copies(), 0);
}

TEST(TaskStore, TakeMovesInsteadOfCopying) {
    BasicTaskStore<Tracked> store;
    store.create_queue("build");
    store.submit("build", Tracked{std::string("payload")});
    Tracked::reset();

    const auto out = store.take("build");
    ASSERT_TRUE(out.has_value());

    EXPECT_EQ(Tracked::copies(), 0);
    EXPECT_GE(Tracked::moves(), 1);
}

TEST(TaskStore, ForEachQueueVisitsQueuesInNameOrder) {
    TaskStore store;
    store.create_queue("b");
    store.create_queue("a");
    store.create_queue("c");
    store.submit("b", Task(2, "x"));
    store.submit("a", Task(1, "x"));
    store.submit("c", Task(3, "x"));

    std::vector<std::string> seen;
    store.for_each_queue([&seen](const std::string& name, const taskq::Queue&) {
        seen.push_back(name);
    });

    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0], std::string("a"));
    EXPECT_EQ(seen[1], std::string("b"));
    EXPECT_EQ(seen[2], std::string("c"));
}

TEST(TaskStore, CaptureByReferenceAccumulatesButByValueDoesNot) {
    TaskStore store;
    store.create_queue("a");
    store.create_queue("b");
    store.submit("a", Task(1, "x"));
    store.submit("a", Task(2, "x"));
    store.submit("b", Task(3, "x"));

    int by_reference = 0;
    store.for_each_queue([&by_reference](const std::string&, const taskq::Queue& t) {
        by_reference += static_cast<int>(t.size());
    });

    int by_value = 0;
    store.for_each_queue([by_value](const std::string&, const taskq::Queue& t) mutable {
        by_value += static_cast<int>(t.size());
    });

    EXPECT_EQ(by_reference, 3);   // 2 + 1
    EXPECT_EQ(by_value, 0);       // менялась копия, наружу ничего не вышло
}

TEST(TaskStore, FindLocatesPendingTaskById) {
    TaskStore store;
    store.create_queue("build");
    store.create_queue("deploy");
    store.submit("build", Task(1, "a", 0));
    store.submit("deploy", Task(2, "b", 0));

    ASSERT_TRUE(store.find(2).has_value());
    EXPECT_EQ(store.find(2)->get().to_string(), std::string("b"));
    EXPECT_EQ(store.queue_of(2).value_or(""), std::string("deploy"));
    EXPECT_FALSE(store.find(3).has_value());
}
