#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "errors.hpp"
#include "task_store.hpp"
#include "tracked.hpp"


TEST(TaskStore, SubmitThenPeek) {
    TaskStore store;
    store.create_queue("build");
    store.submit("build", Task(1, "make all", 5));

    if (const auto front = store.peek("build"); front) {
        EXPECT_EQ(front->id(), 1u);
    } else {
        EXPECT_TRUE(false);
    }
    EXPECT_EQ(store.size("build"), 1u);
}

TEST(TaskStore, LeaseTakesPendingInFifoOrder) {
    TaskStore store;
    store.create_queue("build");
    store.register_worker("w");
    store.submit("build", Task(1, "first", 0));
    store.submit("build", Task(2, "second", 0));

    auto leased = store.lease("build", "w");

    ASSERT_TRUE(leased.has_value());
    EXPECT_EQ(leased->id(), 1u);
    EXPECT_TRUE(leased->state() == State::Leased);

    leased = store.lease("build", "w");

    ASSERT_TRUE(leased.has_value());
    EXPECT_EQ(leased->id(), 2u);
    EXPECT_FALSE(store.lease("build", "w").has_value());
    EXPECT_EQ(store.size("build"), 2u);
}

TEST(TaskStore, EmptyAndMissingQueueAreFinallyDistinct) {
    TaskStore store;
    store.create_queue("build");

    EXPECT_FALSE(store.peek("build").has_value());
    EXPECT_THROW((void)store.peek("absent"), UnknownQueueError);
}

TEST(TaskStore, PeekingMissingQueueCreatesNothing) {
    const TaskStore store;

    EXPECT_THROW((void)store.peek("absent"), UnknownQueueError);
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
    const Tracked named{std::string("payload")};
    Tracked::reset();

    store.submit("build", named);

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


TEST(TaskStore, ForEachQueueVisitsQueuesInNameOrder) {
    TaskStore store;
    store.create_queue("a");
    store.create_queue("b");
    store.create_queue("c");
    store.submit("b", Task(2, "x"));
    store.submit("a", Task(1, "x"));
    store.submit("c", Task(3, "x"));

    std::vector<std::string> seen;
    store.for_each_queue([&seen](const std::string& name, const Queue&) {
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
    store.for_each_queue([&by_reference](const std::string&, const Queue& t) {
        by_reference += static_cast<int>(t.size());
    });

    int by_value = 0;
    store.for_each_queue([by_value](const std::string&, const Queue& t) mutable {
        by_value += static_cast<int>(t.size());
    });

    EXPECT_EQ(by_reference, 3);
    EXPECT_EQ(by_value, 0);
}
