// Этап 9: политика повторов (домашнее задание, Pure Fabrication) и время
// постановки (INFO). Часы — подставные, ни одной реальной паузы.

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "taskq/handler.hpp"
#include "taskq/retry_policy.hpp"
#include "taskq/service.hpp"
#include "manual_clock.hpp"

using std::chrono::seconds;
using taskq::RetryPolicy;
using taskq::Service;
using taskq::TaskStore;
using taskq::testing::ManualClock;

namespace {

std::string reply(Service& service, const std::string& line) {
    return taskq::handle(service, line).text;
}

}  // namespace

TEST(RetryPolicy, DefaultNeverRetries) {
    const RetryPolicy policy;
    EXPECT_EQ(policy.attempts(), 1u);
    EXPECT_FALSE(policy.retry_after(1));
}

TEST(RetryPolicy, BackoffDoublesUntilAttemptsRunOut) {
    const RetryPolicy policy(4, seconds(10));
    EXPECT_EQ(policy.retry_after(1), seconds(10));
    EXPECT_EQ(policy.retry_after(2), seconds(20));
    EXPECT_EQ(policy.retry_after(3), seconds(40));
    EXPECT_FALSE(policy.retry_after(4));   // четвёртая попытка была последней
}

TEST(Retry, FailWithoutPolicyIsFinalAsBefore) {
    Service service;
    for (const char* const line : {"CREATE build", "SUBMIT build a", "POLL build"}) {
        reply(service, line);
    }
    EXPECT_EQ(reply(service, "FAIL 1"), std::string("OK"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("failed"));
}

TEST(Retry, FailedTaskComesBackAfterThePause) {
    ManualClock clock;
    Service service(TaskStore(), clock, nullptr, RetryPolicy(3, seconds(10)));
    for (const char* const line : {"CREATE build", "SUBMIT build a", "POLL build"}) {
        reply(service, line);
    }
    EXPECT_EQ(reply(service, "FAIL 1"), std::string("OK, retry 2 of 3 in 10s"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("pending in build, delayed for 10s"));
    EXPECT_EQ(reply(service, "POLL build"), std::string("(empty)"));   // пауза ещё идёт
    clock.advance(seconds(10));
    EXPECT_EQ(reply(service, "POLL build"), std::string("id=1 a"));
    EXPECT_EQ(reply(service, "FAIL 1"), std::string("OK, retry 3 of 3 in 20s"));
    clock.advance(seconds(20));
    EXPECT_EQ(reply(service, "POLL build"), std::string("id=1 a"));
    EXPECT_EQ(reply(service, "FAIL 1"), std::string("OK"));   // попытки исчерпаны
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("failed"));
}

TEST(Retry, AckForgetsFailures) {
    ManualClock clock;
    Service service(TaskStore(), clock, nullptr, RetryPolicy(2, seconds(1)));
    for (const char* const line : {"CREATE build", "SUBMIT build a", "POLL build", "FAIL 1"}) {
        reply(service, line);
    }
    EXPECT_NE(reply(service, "INFO 1").find("failures=1"), std::string::npos);
    clock.advance(seconds(1));
    reply(service, "POLL build");
    EXPECT_EQ(reply(service, "ACK 1"), std::string("OK"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("done"));
}

TEST(Info, SubmissionTimeIsWallClock) {
    // Время постановки показывают пользователю — оно по системным часам, а
    // не по монотонным, на которых считаются сроки.
    ManualClock clock;
    clock.set_wall(std::chrono::sys_days{std::chrono::year{2026} / 10 / 25} + std::chrono::hours(17));
    Service service(TaskStore(), clock);
    reply(service, "CREATE build");
    reply(service, "SUBMIT build a");
    clock.advance(seconds(90));
    EXPECT_EQ(reply(service, "INFO 1"), std::string("id=1 pending submitted 2026-10-25T17:00:00Z failures=0"));
    reply(service, "POLL build");
    EXPECT_EQ(reply(service, "INFO 1"), std::string("id=1 leased submitted 2026-10-25T17:00:00Z failures=0"));
    reply(service, "ACK 1");
    EXPECT_NE(reply(service, "INFO 1").find("ERR"), std::string::npos);
}
