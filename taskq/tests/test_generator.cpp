// Звёздочка этапа 5: задачи очереди генератором на std::generator.

#include <gtest/gtest.h>

#include <generator>
#include <string>
#include <vector>

#include "taskq/task.hpp"
#include "taskq/task_store.hpp"

namespace {

int& guards_alive() {
    static int count = 0;
    return count;
}

// Локальный объект корутины, считающий себя.
struct Guard {
    Guard() { ++guards_alive(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    ~Guard() { --guards_alive(); }
};

std::generator<int> counting_with_guard(int limit) {
    const Guard guard;
    for (int i = 0; i < limit; ++i) {
        co_yield i;
    }
}

}  // namespace

TEST(Generator, YieldsTasksInSubmissionOrder) {
    taskq::TaskStore store;
    store.create_queue("build");
    store.submit("build", taskq::Task(1, "first", 0));
    store.submit("build", taskq::Task(2, "second", 9));
    std::vector<std::string> seen;
    for (const taskq::Task& task : store.tasks_lazily("build")) {
        seen.push_back(task.to_string());
    }
    EXPECT_EQ(seen, (std::vector<std::string>{"first", "second"}));
}

TEST(Generator, UnknownQueueYieldsNothing) {
    const taskq::TaskStore store;
    int count = 0;
    for (const taskq::Task& task : store.tasks_lazily("absent")) {
        static_cast<void>(task);
        ++count;
    }
    EXPECT_EQ(count, 0);
}

TEST(Generator, EarlyExitDestroysCoroutineLocals) {
    guards_alive() = 0;
    {
        auto numbers = counting_with_guard(1000);
        for (const int i : numbers) {
            EXPECT_EQ(guards_alive(), 1);   // корутина приостановлена, Guard жив
            if (i == 2) {
                break;
            }
        }
        EXPECT_EQ(guards_alive(), 1);
    }
    // Генератор разрушен — кадр корутины уничтожен в точке приостановки, и
    // Guard разрушен корректно: RAII работает и внутри корутины.
    EXPECT_EQ(guards_alive(), 0);
}
