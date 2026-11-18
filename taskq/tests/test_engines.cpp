// Этап 8: одни и те же проверки на каждом движке индекса по номеру.
// Остальные тесты гоняются на всех движках через TASKQ_ENGINE
// (CMakeLists.txt); эти — явно параметризованы.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <string>

#include "taskq/engines.hpp"
#include "taskq/task_store.hpp"

using taskq::EngineKind;
using taskq::Task;
using taskq::TaskStore;

class Engines : public ::testing::TestWithParam<EngineKind> {};

INSTANTIATE_TEST_SUITE_P(All, Engines, ::testing::ValuesIn(taskq::all_engine_kinds),
                         [](const ::testing::TestParamInfo<EngineKind>& param) {
                             std::string name(taskq::name_of(param.param));
                             std::erase(name, '-');   // имя теста gtest — только буквы и цифры
                             return name;
                         });

TEST_P(Engines, ReportsItsName) {
    const TaskStore store(GetParam());
    EXPECT_EQ(store.engine_name(), taskq::name_of(GetParam()));
}

TEST_P(Engines, FindsEveryTaskAcrossGrowth) {
    TaskStore store(GetParam());
    store.create_queue("build");
    for (taskq::TaskId id = 1; id <= 3000; ++id) {
        store.submit("build", Task(id, "t" + std::to_string(id), static_cast<int>(id % 10)));
    }
    for (taskq::TaskId id = 1; id <= 3000; id += 97) {
        ASSERT_TRUE(store.find(id)) << id;
        EXPECT_EQ(store.find(id)->get().to_string(), "t" + std::to_string(id));
        EXPECT_EQ(store.queue_of(id), std::string("build"));
    }
    EXPECT_FALSE(store.find(3001));
}

TEST_P(Engines, QueueChurnKeepsTheIndexExact) {
    // Скользящее окно: выдаётся старейшая задача высшего приоритета, ставится
    // новая. Через индекс проходят тысячи номеров, живых — сотня.
    TaskStore store(GetParam());
    store.create_queue("q");
    taskq::TaskId next = 1;
    for (; next <= 100; ++next) {
        store.submit("q", Task(next, "x"));
    }
    for (int round = 0; round < 5000; ++round) {
        const auto taken = store.take("q");
        ASSERT_TRUE(taken);
        EXPECT_FALSE(store.find(taken->id()));
        store.submit("q", Task(next++, "x"));
    }
    EXPECT_EQ(store.size("q"), 100u);
    EXPECT_TRUE(store.find(next - 1));
    EXPECT_FALSE(store.find(next - 101));
}

TEST_P(Engines, DelayedTasksAreFoundByIdToo) {
    TaskStore store(GetParam());
    store.create_queue("q");
    store.submit("q", Task(1, "later"));
    const taskq::Instant now{};
    ASSERT_TRUE(store.delay(1, now + std::chrono::hours(1)));
    ASSERT_TRUE(store.find(1));
    EXPECT_EQ(store.find(1)->get().to_string(), "later");
    EXPECT_EQ(store.queue_of(1), std::string("q"));
    EXPECT_FALSE(store.peek("q", now));
}
