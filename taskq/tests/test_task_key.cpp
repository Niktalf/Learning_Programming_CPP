// Этап 5: оператор <=> для ключа выборки. Здесь порядок важнее, чем в других
// вариантах: он определяет, какую работу выполнят раньше.

#include <gtest/gtest.h>

#include <algorithm>
#include <compare>
#include <string>
#include <vector>

#include "taskq/service.hpp"
#include "taskq/handler.hpp"
#include "taskq/task_key.hpp"

using taskq::PlainTaskKey;
using taskq::TaskKey;

TEST(TaskKey, DefaultOrderIsWrongForPriority) {
    // = default сравнивает поля по возрастанию: задача с приоритетом 0
    // «меньше» задачи с приоритетом 9 и выдавалась бы первой. Ровно та
    // ошибка, которую = default делает молча.
    EXPECT_LT((PlainTaskKey{0, 1}), (PlainTaskKey{9, 2}));
}

TEST(TaskKey, HigherPriorityFirstThenOlder) {
    // «Меньше» значит «выдать раньше».
    EXPECT_LT((TaskKey{9, 5, ""}), (TaskKey{0, 1, ""}));   // приоритет по убыванию
    EXPECT_LT((TaskKey{5, 1, ""}), (TaskKey{5, 2, ""}));   // номер по возрастанию
    EXPECT_GT((TaskKey{5, 2, ""}), (TaskKey{5, 1, ""}));
}

TEST(TaskKey, EqualityIgnoresRequester) {
    // Без отдельно объявленного operator== эта строка не скомпилировалась бы:
    // из написанного руками <=> равенство не выводится.
    EXPECT_EQ((TaskKey{3, 10, "alice"}), (TaskKey{3, 10, "bob"}));
}

TEST(TaskKey, SortingMixedPriorities) {
    std::vector<TaskKey> keys = {{0, 1, ""}, {9, 2, ""}, {5, 3, ""}, {9, 4, ""}, {0, 5, ""}};
    std::ranges::sort(keys);
    std::vector<int> priorities;
    std::vector<std::uint64_t> order;
    for (const TaskKey& key : keys) {
        priorities.push_back(key.priority);
        order.push_back(key.seq);
    }
    EXPECT_EQ(priorities, (std::vector<int>{9, 9, 5, 0, 0}));
    EXPECT_EQ(order, (std::vector<std::uint64_t>{2, 4, 3, 1, 5}));
}

TEST(Service, PollRespectsPriorityThenOrder) {
    // Главный тест этапа для этого варианта: смешанные приоритеты. Ошибка в
    // знаке сравнения приоритета видна только здесь.
    taskq::Service service;
    for (const char* const line : {"CREATE build", "SUBMIT build low-1", "SUBMIT build prio=9 urgent-1",
                                   "SUBMIT build prio=5 mid", "SUBMIT build prio=9 urgent-2",
                                   "SUBMIT build low-2"}) {
        static_cast<void>(taskq::handle(service, line));
    }
    std::vector<std::string> polled;
    for (int i = 0; i < 5; ++i) {
        const std::string text = taskq::handle(service, "POLL build").text;
        polled.push_back(text.substr(text.find(' ') + 1));
    }
    EXPECT_EQ(polled, (std::vector<std::string>{"urgent-1", "urgent-2", "mid", "low-1", "low-2"}));
}

TEST(Service, ReleasedTaskGoesBehindItsPriorityPeers) {
    taskq::Service service;
    for (const char* const line : {"CREATE build", "SUBMIT build prio=5 first", "SUBMIT build prio=5 second",
                                   "POLL build", "RELEASE 1"}) {
        static_cast<void>(taskq::handle(service, line));
    }
    // Возврат аренды — новая постановка: задача встаёт за равными по
    // приоритету, а не возвращается вперёд.
    EXPECT_EQ(taskq::handle(service, "POLL build").text, std::string("id=2 second"));
    EXPECT_EQ(taskq::handle(service, "POLL build").text, std::string("id=1 first"));
}
