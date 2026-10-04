#include <gtest/gtest.h>

#include <string>

#include "handler.hpp"

namespace {
    bool starts_with(const std::string& s, const std::string& prefix) {
        return s.compare(0, prefix.size(), prefix) == 0;
    }
}  // namespace

TEST(Handler, FullLifecycle) {
    TaskStore store;
    Service service{store};
    (void)handle(service, "CREATE build");

    EXPECT_EQ(handle(service, "SUBMIT build 5 make all").text, std::string("OK id=1"));
    EXPECT_TRUE(starts_with(handle(service, "POLL build").text, "id=1"));
    EXPECT_TRUE(handle(service, "ACK 1").ok);
}

TEST(Handler, EmptyQueueIsNotAnError) {
    TaskStore store;
    Service service{store};
    (void)handle(service, "CREATE build");
    const auto [ok, text] = handle(service, "POLL build");

    EXPECT_TRUE(ok);
    EXPECT_EQ(text, std::string("(queue is empty)"));
}

TEST(Handler, MissingQueueIsAnError) {
    TaskStore store;
    Service service{store};
    const auto [ok, text] = handle(service, "POLL absent");

    EXPECT_FALSE(ok);
    EXPECT_EQ(text, std::string("ERR no such queue: absent"));
}

TEST(Handler, AcknowledgingUnleasedTaskIsInvalidTransition) {
    TaskStore store;
    Service service{store};
    (void)handle(service, "CREATE build");
    (void)handle(service, "SUBMIT build 5 make all");
    const auto [ok, text] = handle(service, "ACK 1");

    EXPECT_FALSE(ok);
    EXPECT_EQ(text, std::string("ERR task 1 is pending, cannot become done"));
}

TEST(Handler, DoubleAcknowledgeIsInvalidTransition) {
    TaskStore store;
    Service service{store};
    (void)handle(service, "CREATE build");
    (void)handle(service, "SUBMIT build 5 make all");
    (void)handle(service, "POLL build");

    EXPECT_TRUE(handle(service, "ACK 1").ok);
    EXPECT_FALSE(handle(service, "ACK 1").ok);
}

TEST(Handler, UnknownTaskIsAnError) {
    TaskStore store;
    Service service{store};
    const auto [ok, text] = handle(service, "ACK 999");

    EXPECT_FALSE(ok);
    EXPECT_EQ(text, std::string("ERR no such task: 999"));
}

TEST(Handler, ReleasedTaskCanBeLeasedAgain) {
    TaskStore store;
    Service service{store};
    (void)handle(service, "CREATE build");
    (void)handle(service, "SUBMIT build 5 make all");
    (void)handle(service, "POLL build");

    EXPECT_TRUE(handle(service, "RELEASE 1").ok);
    EXPECT_TRUE(starts_with(handle(service, "POLL build").text, "id=1"));
}

TEST(Handler, NeverThrowsOnGarbage) {
    TaskStore store;
    Service service{store};

    EXPECT_NO_THROW((void)handle(service, ""));
    EXPECT_NO_THROW((void)handle(service, "ACK"));
    EXPECT_NO_THROW((void)handle(service, "ACK 99999999999999999999999"));
    EXPECT_NO_THROW((void)handle(service, "SUBMIT \"open 5 x"));
}
