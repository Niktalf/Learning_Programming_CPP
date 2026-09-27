#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "executor.hpp"
#include "parser.hpp"


namespace {

Response run(Service& service, std::string text) {
    const InputLine line{std::move(text)};
    const auto command = parse(line);
    if (!command) {
        return Response{false, "(not parsed)"};
    }
    return service.execute(*command);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(Executor, SubmitAssignsIncreasingIds) {
    TaskStore store;
    Service service{store};

    EXPECT_EQ(run(service, "SUBMIT build 5 make all").text, std::string("OK id=1"));
    EXPECT_EQ(run(service, "SUBMIT build 5 make test").text, std::string("OK id=2"));
}

TEST(Executor, PollTakesInFifoOrder) {
    TaskStore store;
    Service service{store};
    run(service, "SUBMIT build 5 first");
    run(service, "SUBMIT build 5 second");

    EXPECT_TRUE(contains(run(service, "POLL build").text, "first"));
    EXPECT_TRUE(contains(run(service, "POLL build").text, "second"));
    EXPECT_FALSE(run(service, "POLL build").ok);
}

TEST(Executor, EmptyAndMissingQueueAreIndistinguishable) {
    TaskStore store;
    Service service{store};
    store.create_queue("build");

    const auto [empty_ok, empty_text] = run(service, "POLL build");
    const auto [missing_ok, missing_text] = run(service, "POLL absent");

    EXPECT_FALSE(empty_ok);
    EXPECT_FALSE(missing_ok);
    EXPECT_EQ(empty_text, missing_text);
}

TEST(Executor, StatusDoesNotConsume) {
    TaskStore store;
    Service service{store};
    run(service, "SUBMIT build 5 make all");

    EXPECT_TRUE(run(service, "STATUS build").ok);
    EXPECT_TRUE(run(service, "STATUS build").ok);
    EXPECT_TRUE(run(service, "POLL build").ok);
}

TEST(Executor, TypeReportsAlternative) {
    TaskStore store;
    Service service{store};
    run(service, "SUBMIT build 5 make all");
    EXPECT_EQ(run(service, "TYPE build").text, std::string("text"));
}

TEST(Executor, BumpOnTextFailsWithoutThrowing) {
    TaskStore store;
    Service service{store};
    run(service, "SUBMIT build 5 make all");

    EXPECT_FALSE(run(service, "BUMP build").ok);
}

TEST(Executor, BumpOnNumericAccumulates) {
    TaskStore store;
    Service service{store};
    store.submit("build", Task(1, Payload{NumericPayload{10}}, 5));

    EXPECT_EQ(run(service, "BUMP build").text, std::string("11"));
    EXPECT_EQ(run(service, "BUMP build 9").text, std::string("20"));
}
