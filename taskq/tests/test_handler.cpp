#include <gtest/gtest.h>

#include <string>

#include "taskq/executor.hpp"
#include "taskq/handler.hpp"

using taskq::handle;
using taskq::Service;

namespace {

std::string reply(Service& service, const std::string& line) {
    return handle(service, line).text;
}

}  // namespace

TEST(Handler, SuccessPassesThrough) {
    Service service;
    EXPECT_EQ(reply(service, "CREATE build"), std::string("OK"));
    EXPECT_EQ(reply(service, "SUBMIT build make"), std::string("OK id=1"));
}

TEST(Handler, EachErrorBecomesResponse) {
    Service service;
    static_cast<void>(handle(service, "CREATE build"));
    static_cast<void>(handle(service, "SUBMIT build make"));

    EXPECT_EQ(reply(service, "FROB 1"), std::string("ERR unknown command: FROB"));
    EXPECT_EQ(reply(service, "POLL absent"),
              std::string("ERR no such queue: absent (use CREATE) (while executing POLL absent)"));
    EXPECT_EQ(reply(service, "ACK 9"), std::string("ERR no such task: 9 (while executing ACK 9)"));
    EXPECT_EQ(reply(service, "ACK 1"),
              std::string("ERR task 1 is pending, cannot become done (while executing ACK 1)"));
    EXPECT_EQ(reply(service, "BUMP 1"),
              std::string("ERR wrong type: expected number, got text (while executing BUMP 1)"));
    EXPECT_EQ(reply(service, "SUBMIT build num:x"),
              std::string("ERR num: payload must be a 64-bit integer (while executing SUBMIT build)"));
}

TEST(Handler, ResponseIsMarkedFailed) {
    Service service;
    EXPECT_FALSE(handle(service, "POLL absent").ok);
    EXPECT_TRUE(handle(service, "CREATE build").ok);
}

TEST(Handler, NeverThrowsOnGarbage) {
    Service service;
    // Граница не выпускает наружу ничего — это и есть её назначение.
    EXPECT_NO_THROW(static_cast<void>(handle(service, "")));
    EXPECT_NO_THROW(static_cast<void>(handle(service, "\"\"\"")));
    EXPECT_NO_THROW(static_cast<void>(handle(service, "BUMP 99999999999999999999999")));
    EXPECT_NO_THROW(static_cast<void>(handle(service, "SUBMITBATCH \"")));
}

TEST(Handler, FailedBatchDoesNotCorruptState) {
    Service service;
    static_cast<void>(handle(service, "CREATE build"));
    static_cast<void>(handle(service, "SUBMITBATCH build a absent b"));
    EXPECT_EQ(reply(service, "POLL build"), std::string("(empty)"));
}
