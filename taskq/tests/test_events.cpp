// Этап 10: доменные события сервиса задач — какой переход что публикует. Без
// брокера: издатель подставной (FakePublisher), часы — ManualClock.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <variant>
#include <vector>

#include "fake_publisher.hpp"
#include "manual_clock.hpp"
#include "taskq/events.hpp"
#include "taskq/handler.hpp"
#include "taskq/service.hpp"

using std::chrono::seconds;
using taskq::DomainEvent;
using taskq::Service;
using taskq::testing::FakePublisher;
using taskq::testing::ManualClock;

namespace {

struct Fixture {
    explicit Fixture(taskq::RetryPolicy retry = taskq::RetryPolicy())
        : service(taskq::TaskStore(), clock, nullptr, retry, publisher.port()) {
        static_cast<void>(run("CREATE build"));
        publisher.clear();
    }

    bool run(const std::string& line) { return taskq::handle(service, line).ok; }
    [[nodiscard]] const std::vector<DomainEvent>& events() const { return publisher.events(); }

    ManualClock clock;
    FakePublisher publisher;
    Service service;
};

template <class E>
[[nodiscard]] const E& as(const DomainEvent& event) {
    return std::get<E>(event);
}

}  // namespace

TEST(Events, FullCycleInOrder) {
    // Постановка, выдача, подтверждение: три перехода — три события, в
    // порядке автомата.
    Fixture f;
    ASSERT_TRUE(f.run("WORKER w1"));
    ASSERT_TRUE(f.run("SUBMIT build prio=4 make"));
    ASSERT_TRUE(f.run("POLL build w1"));
    ASSERT_TRUE(f.run("ACK 1"));
    ASSERT_EQ(f.events().size(), 3u);
    EXPECT_EQ(as<taskq::TaskSubmitted>(f.events()[0]).queue, "build");
    EXPECT_EQ(as<taskq::TaskSubmitted>(f.events()[0]).priority, 4);
    EXPECT_EQ(as<taskq::TaskLeased>(f.events()[1]).worker, "w1");
    EXPECT_EQ(as<taskq::TaskCompleted>(f.events()[2]).id, 1u);
}

TEST(Events, BatchPublishesEveryTask) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(SUBMITBATCH build "make all" build test)"));
    ASSERT_EQ(f.events().size(), 2u);
    EXPECT_EQ(taskq::task_of(f.events()[0]), 1u);
    EXPECT_EQ(taskq::task_of(f.events()[1]), 2u);
}

TEST(Events, FailedBatchPublishesNothing) {
    Fixture f;
    EXPECT_FALSE(f.run("SUBMITBATCH build a absent b"));
    EXPECT_TRUE(f.events().empty());
}

TEST(Events, AnonymousLeaseHasNoWorker) {
    Fixture f;
    ASSERT_TRUE(f.run("SUBMIT build make"));
    ASSERT_TRUE(f.run("POLL build"));
    EXPECT_TRUE(as<taskq::TaskLeased>(f.events()[1]).worker.empty());
}

TEST(Events, ReleaseReturnsTheTask) {
    Fixture f;
    ASSERT_TRUE(f.run("SUBMIT build make"));
    ASSERT_TRUE(f.run("POLL build"));
    ASSERT_TRUE(f.run("RELEASE 1"));
    ASSERT_EQ(f.events().size(), 3u);
    EXPECT_EQ(as<taskq::TaskReturned>(f.events()[2]).reason, "released");
}

TEST(Events, FinalFailureNamesTheAttempt) {
    Fixture f;
    ASSERT_TRUE(f.run("SUBMIT build make"));
    ASSERT_TRUE(f.run("POLL build"));
    ASSERT_TRUE(f.run("FAIL 1"));
    EXPECT_EQ(as<taskq::TaskFailed>(f.events().back()).reason, "attempt 1 of 1");
}

TEST(Events, RetryIsReturnThenFinalFailure) {
    Fixture f(taskq::RetryPolicy(2, seconds(0)));
    ASSERT_TRUE(f.run("SUBMIT build make"));
    ASSERT_TRUE(f.run("POLL build"));
    ASSERT_TRUE(f.run("FAIL 1"));   // повтор
    ASSERT_TRUE(f.run("POLL build"));
    ASSERT_TRUE(f.run("FAIL 1"));   // окончательно
    ASSERT_EQ(f.events().size(), 5u);
    EXPECT_EQ(as<taskq::TaskReturned>(f.events()[2]).reason, "retry 2 of 2");
    EXPECT_TRUE(std::holds_alternative<taskq::TaskLeased>(f.events()[3]));
    EXPECT_EQ(as<taskq::TaskFailed>(f.events()[4]).reason, "attempt 2 of 2");
}

TEST(Events, ReapNamesTheCause) {
    Fixture f;
    f.service.set_lease_timeout(seconds(30));
    ASSERT_TRUE(f.run("WORKER w1"));
    ASSERT_TRUE(f.run("SUBMIT build a"));
    ASSERT_TRUE(f.run("SUBMIT build b"));
    ASSERT_TRUE(f.run("POLL build w1"));   // задача 1 — воркеру w1
    ASSERT_TRUE(f.run("POLL build"));      // задача 2 — анонимно
    ASSERT_TRUE(f.run("GONE w1"));
    f.clock.advance(seconds(31));
    f.publisher.clear();
    ASSERT_TRUE(f.run("REAP"));
    ASSERT_EQ(f.events().size(), 2u);
    EXPECT_EQ(as<taskq::TaskReturned>(f.events()[0]).reason, "abandoned");   // и просрочена, но держателя нет
    EXPECT_EQ(as<taskq::TaskReturned>(f.events()[1]).reason, "expired");
}

TEST(Events, PurgeCancelsPendingOnly) {
    Fixture f;
    ASSERT_TRUE(f.run("SUBMIT build a"));
    ASSERT_TRUE(f.run("SUBMIT build b"));
    ASSERT_TRUE(f.run("POLL build"));
    f.publisher.clear();
    ASSERT_TRUE(f.run("PURGE build"));
    ASSERT_EQ(f.events().size(), 1u);
    EXPECT_TRUE(std::holds_alternative<taskq::TaskCancelled>(f.events()[0]));
}

TEST(Events, RejectedTransitionsPublishNothing) {
    Fixture f;
    ASSERT_TRUE(f.run("SUBMIT build make"));
    f.publisher.clear();
    EXPECT_FALSE(f.run("ACK 1"));      // ещё не выдана
    EXPECT_FALSE(f.run("FAIL 1"));
    EXPECT_FALSE(f.run("RELEASE 1"));
    EXPECT_FALSE(f.run("ACK 99"));
    EXPECT_TRUE(f.events().empty());
}

TEST(Events, ReadsAndEditsPublishNothing) {
    Fixture f;
    ASSERT_TRUE(f.run("SUBMIT build num:41"));
    f.publisher.clear();
    for (const char* command : {"STATUS 1", "TYPE 1", "LIST build", "PEEK build", "INFO 1", "BUMP 1", "DELAY 1 5"}) {
        ASSERT_TRUE(f.run(command)) << command;
    }
    EXPECT_TRUE(f.events().empty());
}

TEST(Events, PublisherFailureLosesEventButNotTransition) {
    Fixture f;
    ASSERT_TRUE(f.run("SUBMIT build make"));
    ASSERT_TRUE(f.run("POLL build"));
    f.publisher.fail(true);
    EXPECT_TRUE(f.run("ACK 1"));
    EXPECT_EQ(f.service.lost_events(), 1u);
    EXPECT_EQ(taskq::handle(f.service, "STATUS 1").text, "done");
}

TEST(Events, EachCommandPublishesOnlyItsOwnEvents) {
    // Отказ издателя не оставляет событий команды на следующую.
    Fixture f;
    f.publisher.fail(true);
    ASSERT_TRUE(f.run("SUBMIT build make"));
    f.publisher.fail(false);
    ASSERT_TRUE(f.run("POLL build"));
    ASSERT_EQ(f.events().size(), 1u);
    EXPECT_TRUE(std::holds_alternative<taskq::TaskLeased>(f.events()[0]));
}

TEST(Events, FinalEvents) {
    EXPECT_TRUE(taskq::is_final(taskq::TaskCompleted{1}));
    EXPECT_TRUE(taskq::is_final(taskq::TaskFailed{1, "x"}));
    EXPECT_TRUE(taskq::is_final(taskq::TaskCancelled{1}));
    EXPECT_FALSE(taskq::is_final(taskq::TaskSubmitted{1, "q", 0}));
    EXPECT_FALSE(taskq::is_final(taskq::TaskLeased{1, ""}));
    EXPECT_FALSE(taskq::is_final(taskq::TaskReturned{1, "released"}));
}
