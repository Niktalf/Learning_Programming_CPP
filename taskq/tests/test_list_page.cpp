// Этап 8: LIST с курсором.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "taskq/handler.hpp"
#include "taskq/service.hpp"
#include "taskq/task_store.hpp"

using taskq::Task;
using taskq::TaskKey;
using taskq::TaskStore;

namespace {

const auto any = [](const Task&) { return true; };

TaskStore numbered(int count) {
    TaskStore store;
    store.create_queue("q");
    for (int i = 1; i <= count; ++i) {
        store.submit("q", Task(static_cast<taskq::TaskId>(i), "t", i % 3));
    }
    return store;
}

}  // namespace

TEST(ListPage, PagesCoverTheQueueOnceInPollOrder) {
    const TaskStore store = numbered(50);
    std::vector<taskq::TaskId> seen;
    std::optional<TaskKey> cursor;
    int pages = 0;
    for (;;) {
        const auto page = store.list_page("q", 0, any, 20, cursor);
        for (const Task& task : page.tasks) {
            seen.push_back(task.id());
        }
        ++pages;
        if (!page.more) {
            break;
        }
        cursor = page.tasks.back().get().key();
    }
    EXPECT_EQ(pages, 3);
    ASSERT_EQ(seen.size(), 50u);
    EXPECT_EQ(std::set<taskq::TaskId>(seen.begin(), seen.end()).size(), 50u);
    // Порядок выдачи: сначала приоритет 2, потом 1, потом 0.
    EXPECT_EQ(store.find(seen.front())->get().priority(), 2);
    EXPECT_EQ(store.find(seen.back())->get().priority(), 0);
}

TEST(ListPage, PollsBetweenPagesDoNotShiftTheCursor) {
    // Курсор — ключ выдачи, а не номер позиции: задачи, выданные между
    // страницами, не заставляют оставшиеся пропасть или повториться.
    TaskStore store = numbered(10);
    const auto first = store.list_page("q", 0, any, 4, std::nullopt);
    ASSERT_TRUE(first.more);
    const TaskKey cursor = first.tasks.back().get().key();
    static_cast<void>(store.take("q"));   // выдали первую — она уже показана
    const auto second = store.list_page("q", 0, any, 100, cursor);
    EXPECT_EQ(second.tasks.size(), 6u);
    EXPECT_FALSE(second.more);
}

TEST(ListPage, FiltersApplyAfterTheCursor) {
    const TaskStore store = numbered(30);
    const auto page = store.list_page("q", 1, any, 5, std::nullopt);   // только приоритеты 1 и 2
    ASSERT_EQ(page.tasks.size(), 5u);
    EXPECT_TRUE(page.more);
    const auto rest = store.list_page("q", 1, any, 100, page.tasks.back().get().key());
    EXPECT_EQ(page.tasks.size() + rest.tasks.size(), 20u);
}

TEST(ListPage, CommandRoundTrip) {
    taskq::Service service;
    static_cast<void>(taskq::handle(service, "CREATE build"));
    for (const char* const body : {"a", "b", "c"}) {
        static_cast<void>(taskq::handle(service, std::string("SUBMIT build ") + body));
    }
    EXPECT_EQ(taskq::handle(service, "LIST build limit=2").text,
              std::string("id=1 prio=0 a\nid=2 prio=0 b\n(more: after=0.2)"));
    EXPECT_EQ(taskq::handle(service, "LIST build limit=2 after=0.2").text, std::string("id=3 prio=0 c"));
    EXPECT_EQ(taskq::handle(service, "LIST build after=0.3").text, std::string("(empty)"));
    EXPECT_EQ(taskq::handle(service, "LIST build after=x").text,
              std::string("ERR cursor must be like 9.17 (priority.sequence): x (usage: LIST <queue> "
                          "[prio>=0..9] [type=text|number|binary] [limit=N] [after=P.S])"));
}
