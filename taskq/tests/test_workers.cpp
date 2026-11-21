// Аренда на weak_ptr: реестр владеет воркерами, выданная задача наблюдает.
// И журнал переходов на уровне сервиса.

#include <gtest/gtest.h>

#include <exception>
#include <string>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/service.hpp"
#include "taskq/handler.hpp"
#include "memory_log.hpp"

using taskq::handle;
using taskq::Service;

namespace {

std::string reply(Service& service, const std::string& line) {
    return handle(service, line).text;
}

// Сервис с очередью build, двумя задачами и воркером w1.
Service prepared() {
    Service service;
    for (const char* const line :
         {"CREATE build", "SUBMIT build first", "SUBMIT build second", "WORKER w1"}) {
        static_cast<void>(handle(service, line));
    }
    return service;
}

}  // namespace

TEST(Workers, LeaseRemembersItsHolder) {
    Service service = prepared();
    EXPECT_EQ(reply(service, "POLL build w1"), std::string("id=1 first"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("leased by w1"));
}

TEST(Workers, VanishedWorkerLeavesTaskAbandoned) {
    Service service = prepared();
    static_cast<void>(handle(service, "POLL build w1"));

    // Реестр отпустил воркера — аренда его не удержала: weak_ptr истёк,
    // и задача видна как брошенная.
    EXPECT_EQ(reply(service, "GONE w1"), std::string("OK"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("leased by w1 (abandoned)"));
}

TEST(Workers, ReapReturnsOnlyAbandonedTasks) {
    Service service = prepared();
    static_cast<void>(handle(service, "WORKER w2"));
    static_cast<void>(handle(service, "POLL build w1"));
    static_cast<void>(handle(service, "POLL build w2"));
    static_cast<void>(handle(service, "GONE w1"));

    EXPECT_EQ(reply(service, "REAP"), std::string("OK returned=1"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("pending in build"));
    EXPECT_EQ(reply(service, "STATUS 2"), std::string("leased by w2"));   // живой — не тронут
    EXPECT_EQ(reply(service, "REAP"), std::string("OK returned=0"));
}

TEST(Workers, AnonymousLeaseIsNeverAbandoned) {
    Service service = prepared();
    static_cast<void>(handle(service, "POLL build"));
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("leased"));
    EXPECT_EQ(reply(service, "REAP"), std::string("OK returned=0"));
}

TEST(Workers, UnknownWorkerIsAnError) {
    Service service = prepared();
    EXPECT_EQ(reply(service, "POLL build ghost"),
              std::string("ERR no such worker: ghost (use WORKER) (while executing POLL build)"));
    // Отказ ничего не выдал: задача по-прежнему ждёт.
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("pending in build"));
    EXPECT_FALSE(handle(service, "GONE ghost").ok);
}

TEST(Workers, ReturningWorkerIsANewObject) {
    Service service = prepared();
    static_cast<void>(handle(service, "POLL build w1"));
    static_cast<void>(handle(service, "GONE w1"));
    static_cast<void>(handle(service, "WORKER w1"));

    // Тот же ник — другой объект. Старая аренда смотрит на исчезнувший,
    // и задача остаётся брошенной: новый w1 её не брал.
    EXPECT_EQ(reply(service, "STATUS 1"), std::string("leased by w1 (abandoned)"));
}

TEST(Workers, JournalRecordsTransitions) {
    // Этап 9: журнал — порт, и тест подставляет журнал в памяти. Файловая
    // версия — в test_compose.cpp (taskq_integration_tests).
    const taskq::testing::MemoryLog journal;
    Service service(taskq::TaskStore(), taskq::system_clocks(), journal.sink());
    for (const char* const line : {"CREATE build", "SUBMIT build a", "WORKER w1",
                                   "POLL build w1", "GONE w1", "REAP", "POLL build",
                                   "ACK 1", "ACK 1"}) {
        static_cast<void>(handle(service, line));
    }
    EXPECT_EQ(journal.records(), (std::vector<std::string>{"1\t-\tpending", "1\tpending\tleased",
                                                           "1\tleased\tpending", "1\tpending\tleased",
                                                           "1\tleased\tdone"}));
}
