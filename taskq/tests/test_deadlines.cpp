// Этап 8: индекс по сроку — отложенные задачи и аренды.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "taskq/handler.hpp"
#include "taskq/service.hpp"
#include "taskq/task_store.hpp"
#include "manual_clock.hpp"

using std::chrono::seconds;
using taskq::Service;
using taskq::Task;
using taskq::TaskStore;
using taskq::Instant;
using taskq::testing::ManualClock;

namespace {

std::string reply(Service& service, const std::string& line) {
    return taskq::handle(service, line).text;
}

// Этап 9: подставные часы — порт IClock, переданный в конструктор.
Service build_service(const ManualClock& clock) {
    Service service(TaskStore(), clock);
    static_cast<void>(reply(service, "CREATE build"));
    return service;
}

}  // namespace

TEST(Deadlines, DelayedTaskLeavesTheQueue) {
    // Отложенная задача — не в очереди, а в индексе по сроку: вершина очереди
    // — всегда готовая задача, и POLL не проходит мимо отложенных.
    TaskStore store;
    store.create_queue("q");
    store.submit("q", Task(1, "a", 9));
    store.submit("q", Task(2, "b"));
    const Instant now{};   // этап 9: монотонный момент, без настоящих часов
    ASSERT_TRUE(store.delay(1, now + seconds(60)));
    EXPECT_EQ(store.view("q").size(), 1u);   // в очереди — только готовая
    EXPECT_EQ(store.size("q"), 2u);          // ожидающих — обе
    EXPECT_EQ(store.delayed_count(), 1u);
    EXPECT_EQ(store.front_id("q", now), 2u);
    EXPECT_TRUE(store.due(now).empty());
    EXPECT_EQ(store.due(now + seconds(60)), (std::vector<taskq::TaskId>{1}));
}

TEST(Deadlines, DueTaskWinsByPriorityEvenBeforePromotion) {
    // Срок наступил, но задача ещё в индексе по сроку: peek и take видят её
    // там и сравнивают с вершиной очереди по ключу выдачи.
    TaskStore store;
    store.create_queue("q");
    store.submit("q", Task(1, "urgent", 9));
    store.submit("q", Task(2, "routine"));
    const Instant now{};   // этап 9: монотонный момент, без настоящих часов
    store.delay(1, now + seconds(10));
    EXPECT_EQ(store.front_id("q", now + seconds(10)), 1u);
    const auto taken = store.take("q", now + seconds(10));
    ASSERT_TRUE(taken);
    EXPECT_EQ(taken->id(), 1u);
    EXPECT_EQ(store.delayed_count(), 0u);
    EXPECT_FALSE(store.find(1));
}

TEST(Deadlines, PromoteReturnsOnlyDueTasksInOrder) {
    TaskStore store;
    store.create_queue("a");
    store.create_queue("b");
    store.submit("a", Task(1, "x"));
    store.submit("b", Task(2, "y"));
    store.submit("a", Task(3, "z"));
    const Instant now{};   // этап 9: монотонный момент, без настоящих часов
    store.delay(1, now + seconds(30));
    store.delay(2, now + seconds(10));
    store.delay(3, now + seconds(90));
    EXPECT_EQ(store.due(now + seconds(30)), (std::vector<taskq::TaskId>{2, 1}));   // по сроку
    EXPECT_EQ(store.promote_due(now + seconds(30)), 2u);
    EXPECT_EQ(store.delayed_count(), 1u);
    EXPECT_EQ(store.view("a").size(), 1u);
    EXPECT_EQ(store.view("b").size(), 1u);
    EXPECT_FALSE(store.not_before(1));
    EXPECT_EQ(store.not_before(3), now + seconds(90));
}

TEST(Deadlines, RedelayMovesTheDeadline) {
    TaskStore store;
    store.create_queue("q");
    store.submit("q", Task(1, "x"));
    const Instant now{};   // этап 9: монотонный момент, без настоящих часов
    store.delay(1, now + seconds(100));
    store.delay(1, now + seconds(5));
    EXPECT_EQ(store.not_before(1), now + seconds(5));
    EXPECT_EQ(store.delayed_count(), 1u);
    EXPECT_EQ(store.front_id("q", now + seconds(5)), 1u);
}

TEST(Deadlines, PurgeTakesDelayedTasksToo) {
    ManualClock clock;
    Service service = build_service(clock);
    reply(service, "SUBMIT build a");
    reply(service, "SUBMIT build b");
    reply(service, "DELAY 1 60");
    EXPECT_EQ(reply(service, "PURGE build"), std::string("OK purged=2"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("cancelled"));
    EXPECT_EQ(service.store().delayed_count(), 0u);
}

TEST(Deadlines, ListShowsReadyTasksOnly) {
    ManualClock clock;
    Service service = build_service(clock);
    reply(service, "SUBMIT build a");
    reply(service, "SUBMIT build b");
    reply(service, "DELAY 1 60");
    EXPECT_EQ(reply(service, "LIST build"), std::string("id=2 prio=0 b"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("pending in build, delayed for 60s"));
}

TEST(Deadlines, ExpiredLeaseIsReaped) {
    ManualClock clock;
    Service service = build_service(clock);
    service.set_lease_timeout(seconds(30));
    reply(service, "SUBMIT build a");
    reply(service, "SUBMIT build b");
    reply(service, "POLL build");   // 1, срок — через 30 с
    clock.advance(seconds(20));
    reply(service, "POLL build");   // 2, срок — через 50 с от начала
    clock.advance(seconds(10));     // 30 с: срок аренды 1 наступил
    EXPECT_EQ(reply(service, "REAP"), std::string("OK returned=1"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("pending in build"));
    EXPECT_EQ(reply(service, "STATUS 2"), std::string("leased"));
    clock.advance(seconds(20));
    EXPECT_EQ(reply(service, "REAP"), std::string("OK returned=1"));
    EXPECT_EQ(reply(service, "REAP"), std::string("OK returned=0"));
}

TEST(Deadlines, AckedLeaseLeavesTheDeadlineIndex) {
    ManualClock clock;
    Service service = build_service(clock);
    service.set_lease_timeout(seconds(30));
    reply(service, "SUBMIT build a");
    reply(service, "POLL build");
    reply(service, "ACK 1");
    clock.advance(seconds(60));
    EXPECT_EQ(reply(service, "REAP"), std::string("OK returned=0"));   // завершённая не возвращается
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("done"));
}

TEST(Deadlines, AbandonedAndExpiredCountedOnce) {
    // Задача и брошена (воркер исчез), и просрочена — вернуться должна один
    // раз: список очищается от повторов sort + unique + erase.
    ManualClock clock;
    Service service = build_service(clock);
    service.set_lease_timeout(seconds(30));
    reply(service, "WORKER w1");
    reply(service, "SUBMIT build a");
    reply(service, "POLL build w1");
    reply(service, "GONE w1");
    clock.advance(seconds(31));
    EXPECT_EQ(reply(service, "REAP"), std::string("OK returned=1"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("pending in build"));
}

TEST(Deadlines, ReapPromotesDueDelayedTasks) {
    ManualClock clock;
    Service service = build_service(clock);
    reply(service, "SUBMIT build a");
    reply(service, "DELAY 1 10");
    clock.advance(seconds(10));
    EXPECT_EQ(reply(service, "REAP"), std::string("OK returned=0, promoted=1"));
    EXPECT_EQ(service.store().delayed_count(), 0u);
    EXPECT_EQ(reply(service, "LIST build"), std::string("id=1 prio=0 a"));
}

TEST(Lease, ExpiresAndReturnsTask) {
    // Этап 9: главный тест этапа для очереди — истечение аренды через десять
    // минут проверяется мгновенно: подставные часы сдвигаются вручную, и
    // сборщик (REAP) видит истёкшую аренду в индексе по сроку.
    ManualClock clock;
    Service service(TaskStore(), clock);
    reply(service, "CREATE build");
    reply(service, "SUBMIT build num:5");
    reply(service, "POLL build");
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("leased"));
    clock.advance(std::chrono::minutes(10));
    EXPECT_EQ(reply(service, "REAP"), std::string("OK returned=1"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("pending in build"));
}
