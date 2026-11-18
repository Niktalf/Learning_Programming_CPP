// Этап 10: формат события в теле сообщения и защита потребителя от повторов
// и пропусков. Без брокера.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>

#include "taskq/event_wire.hpp"
#include "taskq/seen_events.hpp"

using taskq::DomainEvent;
using taskq::EventEnvelope;

TEST(EventWire, SubmittedIsReadable) {
    EXPECT_EQ(taskq::encode_event({"p", 1, taskq::TaskSubmitted{7, "build", -2}}), "p\t1\tsubmitted\t7\tbuild\t-2");
    EXPECT_EQ(taskq::encode_event({"p", 3, taskq::TaskCompleted{7}}), "p\t3\tcompleted\t7");
}

TEST(EventWire, RoundTripEveryKind) {
    for (const DomainEvent& event :
         {DomainEvent(taskq::TaskSubmitted{1, "q", 5}), DomainEvent(taskq::TaskLeased{2, "w1"}),
          DomainEvent(taskq::TaskLeased{3, ""}), DomainEvent(taskq::TaskCompleted{4}),
          DomainEvent(taskq::TaskFailed{5, "attempt 1 of 1"}), DomainEvent(taskq::TaskReturned{6, "expired"}),
          DomainEvent(taskq::TaskCancelled{7})}) {
        const std::optional<EventEnvelope> decoded = taskq::decode_event(taskq::encode_event({"p", 9, event}));
        ASSERT_TRUE(decoded.has_value()) << taskq::encode_event({"p", 9, event});
        EXPECT_EQ(decoded->publisher, "p");
        EXPECT_EQ(decoded->number, 9u);
        EXPECT_EQ(decoded->event.index(), event.index());
        EXPECT_EQ(taskq::task_of(decoded->event), taskq::task_of(event));
        EXPECT_EQ(taskq::describe_event(decoded->event), taskq::describe_event(event));
    }
}

TEST(EventWire, ForeignPayloadsAreRejected) {
    for (const char* payload :
         {"", "hello", "p\t1\tcompleted", "p\t1\tcompleted\tseven", "p\t1\tsubmitted\t7\tq",
          "p\t1\tsubmitted\t7\tq\thigh", "p\t1\tmoved\t7", "\t1\tcompleted\t7", "p\t1\tcompleted\t7\textra",
          "p\t1\tleased\t7\tw\textra"}) {
        EXPECT_FALSE(taskq::decode_event(payload).has_value()) << payload;
    }
}

TEST(EventWire, IdNamesPublisherTaskAndNumber) {
    EXPECT_EQ((EventEnvelope{"p", 4, taskq::TaskCompleted{12}}.id()), "p:12:4");
}

TEST(EventWire, DescribeForHumans) {
    EXPECT_EQ(taskq::describe_event(taskq::TaskSubmitted{1, "build", 3}), "task 1 submitted to build (priority 3)");
    EXPECT_EQ(taskq::describe_event(taskq::TaskLeased{1, "w1"}), "task 1 leased by w1");
    EXPECT_EQ(taskq::describe_event(taskq::TaskLeased{1, ""}), "task 1 leased");
    EXPECT_EQ(taskq::describe_event(taskq::TaskReturned{1, "expired"}), "task 1 returned (expired)");
}

TEST(SeenEvents, RepeatIsRecognisedAndMemoryIsBounded) {
    taskq::SeenEvents seen(2);
    EXPECT_TRUE(seen.first_time("p:1:1"));
    EXPECT_FALSE(seen.first_time("p:1:1"));
    EXPECT_TRUE(seen.first_time("p:1:2"));
    EXPECT_TRUE(seen.first_time("p:1:3"));   // вытесняет первое
    EXPECT_EQ(seen.size(), 2u);
    EXPECT_TRUE(seen.first_time("p:1:1"));   // забыто — цена границы
}

TEST(SequenceTracker, ConsecutiveNumbersHaveNoGap) {
    taskq::SequenceTracker tracker;
    EXPECT_FALSE(tracker.observe("p", 1, 2, false));   // подключились посреди потока
    EXPECT_FALSE(tracker.observe("p", 1, 3, false));
    EXPECT_FALSE(tracker.observe("q", 1, 1, false));   // сервис перезапущен
}

TEST(SequenceTracker, LostEventIsNoticed) {
    taskq::SequenceTracker tracker;
    EXPECT_FALSE(tracker.observe("p", 1, 1, false));
    const auto gap = tracker.observe("p", 1, 3, true);   // пропала выдача: сразу подтверждение
    ASSERT_TRUE(gap.has_value());
    EXPECT_EQ(gap->expected, 2u);
}

TEST(SequenceTracker, FinishedTasksAreForgotten) {
    taskq::SequenceTracker tracker;
    static_cast<void>(tracker.observe("p", 1, 1, false));
    static_cast<void>(tracker.observe("p", 1, 2, false));
    static_cast<void>(tracker.observe("p", 1, 3, true));
    EXPECT_EQ(tracker.tracked(), 0u);
}

TEST(Tally, DuplicatesDoNotCountTwice) {
    // Повтор не доходит до count — его отсекает SeenEvents. Здесь — что
    // каждое событие считается своим счётчиком.
    taskq::Tally tally;
    tally.count(taskq::TaskSubmitted{1, "q", 0});
    tally.count(taskq::TaskLeased{1, ""});
    tally.count(taskq::TaskCompleted{1});
    tally.count(taskq::TaskFailed{2, "attempt 1 of 1"});
    EXPECT_EQ(tally.submitted, 1u);
    EXPECT_EQ(tally.completed, 1u);
    EXPECT_EQ(tally.failed, 1u);
}
