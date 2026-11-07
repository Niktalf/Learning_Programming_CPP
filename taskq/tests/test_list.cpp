// Этап 5: LIST ленивым конвейером. Главное — доказать ленивость счётчиком.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "taskq/executor.hpp"
#include "taskq/handler.hpp"
#include "taskq/task.hpp"
#include "taskq/task_store.hpp"

namespace {

// Тысяча задач: приоритеты по кругу 0..9.
taskq::TaskStore thousand_tasks() {
    taskq::TaskStore store;
    store.create_queue("build");
    for (int i = 0; i < 1000; ++i) {
        store.submit("build", taskq::Task(static_cast<taskq::TaskId>(i + 1), "t" + std::to_string(i), i % 10));
    }
    return store;
}

}  // namespace

TEST(List, PipelineIsLazy) {
    const taskq::TaskStore store = thousand_tasks();
    int calls = 0;
    const auto tasks = store.tasks_where(
        "build", 0,
        [&calls](const taskq::Task&) {
            ++calls;
            return true;
        },
        10);
    EXPECT_EQ(tasks.size(), 10u);
    // Одиннадцать вызовов, а не тысяча: take остановил обход. Одиннадцатый —
    // продвижение filter после последнего элемента: он ищет следующий
    // подходящий, и только потом take видит, что предел набран.
    EXPECT_EQ(calls, 11);
}

TEST(List, PriorityFilterRunsFirst) {
    const taskq::TaskStore store = thousand_tasks();
    int calls = 0;
    const auto tasks = store.tasks_where(
        "build", 9,
        [&calls](const taskq::Task&) {
            ++calls;
            return true;
        },
        3);
    // Второй предикат видит только задачи с приоритетом 9: первый filter
    // отсеял остальные, и до второго они не дошли.
    EXPECT_EQ(tasks.size(), 3u);
    EXPECT_EQ(calls, 4);
    for (const auto& task : tasks) {
        EXPECT_EQ(task.get().priority(), 9);
    }
}

TEST(List, HugeLimitIsClampedNotNegative) {
    // size_t больше PTRDIFF_MAX, переданный в views::take, стал бы
    // отрицательной разностью итераторов. Нашёл фаззер варианта B на таком же
    // конвейере; здесь закрыто тем же способом.
    const taskq::TaskStore store = thousand_tasks();
    const auto all = store.tasks_where("build", 0, [](const taskq::Task&) { return true; },
                                       static_cast<std::size_t>(-1));
    EXPECT_EQ(all.size(), 1000u);
}

TEST(List, CommandWithNamedArguments) {
    taskq::Service service;
    for (const char* const line : {"CREATE build", "SUBMIT build prio=7 make", "SUBMIT build num:1",
                                   "SUBMIT build prio=8 num:2", "SUBMIT build prio=9 test"}) {
        static_cast<void>(taskq::handle(service, line));
    }
    EXPECT_EQ(taskq::handle(service, "LIST build prio>=7 type=number").text,
              std::string("id=3 prio=8 2"));
    EXPECT_EQ(taskq::handle(service, "LIST build limit=2").text,
              std::string("id=1 prio=7 make\nid=2 prio=0 1"));
    EXPECT_EQ(taskq::handle(service, "LIST build type=binary").text, std::string("(empty)"));
    EXPECT_EQ(taskq::handle(service, "LIST build prio>=10").text,
              std::string("ERR priority must be 0..9: 10"));
    EXPECT_EQ(taskq::handle(service, "LIST build limit=1 limit=2").text,
              std::string("ERR unexpected LIST argument: limit=2"));
}
