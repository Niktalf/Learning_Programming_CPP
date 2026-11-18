// Домашнее задание этапа 7, исправление: удаление внутри обхода через
// it = c.erase(it). Ошибочная версия — tools/invalidation_demo.cpp.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>

#include "taskq/task.hpp"
#include "taskq/task_key.hpp"
#include "taskq/task_store.hpp"

namespace {

using Queue = std::map<taskq::PlainTaskKey, std::string>;

Queue thirty_two() {
    Queue queue;
    for (std::uint64_t seq = 1; seq <= 32; ++seq) {
        queue.emplace(taskq::PlainTaskKey{static_cast<int>(seq % 10), seq}, "task " + std::to_string(seq));
    }
    return queue;
}

}  // namespace

TEST(Invalidation, EraseReturnsTheNextValidIterator) {
    Queue queue = thirty_two();
    for (auto it = queue.begin(); it != queue.end();) {
        if (it->first.priority < 5) {
            it = queue.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [key, task] : queue) {
        EXPECT_GE(key.priority, 5) << task;
    }
    EXPECT_EQ(queue.size(), 15u);
}

TEST(Invalidation, EraseIfIsTheShortWay) {
    Queue queue = thirty_two();
    EXPECT_EQ(std::erase_if(queue, [](const auto& entry) { return entry.first.priority < 5; }), 17u);
}

TEST(Invalidation, MapEraseAndInsertKeepOtherIterators) {
    // Правила std::map, на которых стоит индекс по номеру задачи: вставка не
    // делает недействительным ни один итератор, удаление — только итератор
    // удалённого узла. Узлы не переезжают, поэтому индекс может хранить
    // итераторы узлов годами.
    Queue queue = thirty_two();
    const auto kept = queue.find(taskq::PlainTaskKey{7, 7});
    const std::string* const address = &kept->second;
    for (std::uint64_t seq = 100; seq < 1100; ++seq) {
        queue.emplace(taskq::PlainTaskKey{static_cast<int>(seq % 10), seq}, "new");
    }
    std::erase_if(queue, [](const auto& entry) { return entry.first.priority == 3; });
    EXPECT_EQ(&kept->second, address);
    EXPECT_EQ(kept->second, std::string("task 7"));
}

TEST(Invalidation, StoreIndexSurvivesTraffic) {
    // То же на хранилище: тысяча постановок и выдач вокруг задачи не
    // мешает найти её по номеру — запись индекса держит итератор её узла.
    taskq::TaskStore store;
    store.create_queue("q");
    store.submit("q", taskq::Task(1, "keep me", 0));
    for (taskq::TaskId id = 2; id < 1002; ++id) {
        store.submit("q", taskq::Task(id, "noise", 9));
        if (id % 2 == 0) {
            static_cast<void>(store.take("q"));
        }
    }
    EXPECT_EQ(store.find(1)->get().to_string(), std::string("keep me"));
}
