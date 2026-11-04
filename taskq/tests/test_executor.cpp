#include <gtest/gtest.h>

#include <string>

#include "taskq/executor.hpp"
#include "taskq/parser.hpp"

using taskq::Response;
using taskq::Service;

namespace {

// Исполняет одну строку. Строка живёт внутри функции, ответ — независимая
// копия, поэтому её можно возвращать.
Response run(Service& service, const std::string& text) {
    const taskq::InputLine line{text};
    if (const auto command = taskq::parse(line); command) {
        return service.execute(*command);
    }
    return Response{false, "ERR parse error"};
}

}  // namespace

TEST(Executor, SubmitAssignsIncreasingIds) {
    Service service;
    EXPECT_EQ(run(service, "SUBMIT build make all").text, std::string("OK id=1"));
    EXPECT_EQ(run(service, "SUBMIT build make test").text, std::string("OK id=2"));
}

TEST(Executor, PollTakesInFifoOrderAndAckFinishes) {
    Service service;
    run(service, "SUBMIT build first");
    run(service, "SUBMIT build second");

    EXPECT_EQ(run(service, "POLL build").text, std::string("id=1 first"));
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("in progress"));
    EXPECT_EQ(run(service, "STATUS 2").text, std::string("pending in build"));

    EXPECT_TRUE(run(service, "ACK 1").ok);
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("done"));
    EXPECT_FALSE(run(service, "ACK 1").ok);   // дважды подтвердить нельзя
}

TEST(Executor, FailMarksTaskFailed) {
    Service service;
    run(service, "SUBMIT build x");
    run(service, "POLL build");
    EXPECT_TRUE(run(service, "FAIL 1").ok);
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("failed"));
}

TEST(Executor, AckOfPendingTaskIsRejected) {
    Service service;
    run(service, "SUBMIT build x");
    const Response refused = run(service, "ACK 1");
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.text, std::string("ERR task 1 is not in progress"));
}

TEST(Executor, EmptyAndMissingQueueAreIndistinguishable) {
    Service service;
    run(service, "SUBMIT build x");
    run(service, "POLL build");
    // Пустая очередь и несуществующая отвечают одинаково — изъян этапа 2.
    EXPECT_EQ(run(service, "POLL build").text, run(service, "POLL absent").text);
}

TEST(Executor, PayloadKindsAndType) {
    Service service;
    run(service, "SUBMIT build make all");
    run(service, "SUBMIT build num:41");
    run(service, "SUBMIT build bin:cafe");

    EXPECT_EQ(run(service, "TYPE 1").text, std::string("text"));
    EXPECT_EQ(run(service, "TYPE 2").text, std::string("number"));
    EXPECT_EQ(run(service, "TYPE 3").text, std::string("binary"));
    EXPECT_FALSE(run(service, "TYPE 4").ok);
    EXPECT_FALSE(run(service, "SUBMIT build num:x").ok);
    EXPECT_FALSE(run(service, "SUBMIT build bin:abc").ok);
}

TEST(Executor, BumpWorksOnlyOnNumbers) {
    Service service;
    run(service, "SUBMIT build make all");
    run(service, "SUBMIT build num:41");

    EXPECT_EQ(run(service, "BUMP 2").text, std::string("42"));
    EXPECT_EQ(run(service, "BUMP 2 8").text, std::string("50"));

    const Response wrong = run(service, "BUMP 1");
    EXPECT_FALSE(wrong.ok);
    EXPECT_EQ(wrong.text, std::string("ERR BUMP needs a number, the payload is text"));
}
