#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "errors.hpp"
#include "task_store.hpp"


namespace {
    TaskStore store_with_task() {
        TaskStore store;
        store.create_queue("build");
        store.submit("build", Task(1, "make all", 5));
        store.register_worker("w1");
        return store;
    }
}  // namespace

TEST(Lease, TaskRemembersItsHolder) {
    TaskStore store = store_with_task();
    const auto leased = store.lease("build", "w1");

    ASSERT_TRUE(leased.has_value());

    const auto holder = store.find(1).holder();

    ASSERT_NE(holder, nullptr);
    EXPECT_EQ(holder->name(), std::string("w1"));
    EXPECT_FALSE(store.find(1).abandoned());
}

TEST(Lease, TaskDoesNotKeepVanishedWorkerAlive) {
    TaskStore store = store_with_task();
    (void)store.lease("build", "w1");

    const std::weak_ptr<Worker> observer = store.find(1).holder();

    EXPECT_FALSE(observer.expired());
    EXPECT_TRUE(store.remove_worker("w1"));
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(store.find(1).holder(), nullptr);
    EXPECT_TRUE(store.find(1).abandoned());
}

TEST(Lease, FinishedTaskForgetsHolder) {
    TaskStore store = store_with_task();
    (void)store.lease("build", "w1");
    store.transition(1, State::Done);

    EXPECT_EQ(store.find(1).holder(), nullptr);
    EXPECT_FALSE(store.find(1).abandoned());   // выполнена, а не брошена
}

TEST(Lease, UnknownWorkerCannotLease) {
    TaskStore store = store_with_task();

    EXPECT_THROW((void)store.lease("build", "ghost"), UnknownWorkerError);
    EXPECT_TRUE(store.find(1).state() == State::Pending);   // ничего не изменилось
}

TEST(Lease, CopiedTaskObservesTheSameWorker) {
    TaskStore store = store_with_task();
    (void)store.lease("build", "w1");
    const Task copy = store.find(1);
    store.remove_worker("w1");

    EXPECT_TRUE(copy.abandoned());
}
