#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "errors.hpp"
#include "executor.hpp"
#include "parser.hpp"


namespace {
    Response run(Service& service, std::string text) {
        const InputLine line{std::move(text)};
        return service.execute(parse(line));
    }
}  // namespace

TEST(Executor, SubmitAssignsIncreasingIds) {
    TaskStore store;
    Service service{store};
    run(service, "CREATE build");

    EXPECT_EQ(run(service, "SUBMIT build 5 make all").text, std::string("OK id=1"));
    EXPECT_EQ(run(service, "SUBMIT build 5 make test").text, std::string("OK id=2"));
}

TEST(Executor, SubmitToMissingQueueThrows) {
    TaskStore store;
    Service service{store};

    EXPECT_THROW(run(service, "SUBMIT absent 5 x"), UnknownQueueError);
}

TEST(Executor, PollLeasesInFifoOrderWithoutRemoving) {
    TaskStore store;
    Service service{store};
    run(service, "CREATE build");
    run(service, "SUBMIT build 5 first");
    run(service, "SUBMIT build 5 second");

    EXPECT_NE(run(service, "POLL build").text.find("first"), std::string::npos);
    EXPECT_NE(run(service, "POLL build").text.find("second"), std::string::npos);
    EXPECT_EQ(run(service, "POLL build").text, std::string("(queue is empty)"));
    EXPECT_EQ(store.size("build"), 2u);
}

TEST(Executor, InvalidTransitionThrows) {
    TaskStore store;
    Service service{store};
    run(service, "CREATE build");
    run(service, "SUBMIT build 5 x");

    EXPECT_THROW(run(service, "ACK 1"), InvalidTransitionError);
}

TEST(Executor, StatusReportsState) {
    TaskStore store;
    Service service{store};
    run(service, "CREATE build");
    run(service, "SUBMIT build 5 x");
    run(service, "POLL build");
    EXPECT_NE(run(service, "STATUS 1").text.find("leased"), std::string::npos);
}

TEST(Executor, BumpOnTextThrowsTypeMismatch) {
    TaskStore store;
    Service service{store};
    run(service, "CREATE build");
    run(service, "SUBMIT build 5 make all");

    EXPECT_THROW(run(service, "BUMP 1"), TypeMismatchError);
}

TEST(Executor, BumpOnNumericAccumulates) {
    TaskStore store;
    Service service{store};
    store.create_queue("build");
    store.submit("build", Task(1, Payload{NumericPayload{10}}, 5));

    EXPECT_EQ(run(service, "BUMP 1").text, std::string("11"));
    EXPECT_EQ(run(service, "BUMP 1 9").text, std::string("20"));
}

TEST(Executor, BatchCreatesTaskPerQueue) {
    TaskStore store;
    Service service{store};
    run(service, "CREATE build");
    run(service, "CREATE test");

    EXPECT_EQ(run(service, "BATCH build,test 5 make all").text, std::string("OK 2 tasks"));
    EXPECT_EQ(store.size("build"), 1u);
    EXPECT_EQ(store.size("test"), 1u);
}
