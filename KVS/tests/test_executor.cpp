#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "executor.hpp"
#include "parser.hpp"

namespace {
    Response run(Store& store, std::string text) {
        const InputLine line{std::move(text)};
        const auto command = parse(line);
        if (!command) {
            return Response{false, "(не разобрано)"};
        }
        return execute(store, *command);
    }
}  // namespace

TEST(Executor, SetThenGet) {
    Store store;
    EXPECT_TRUE(run(store, "SET k hello").ok);
    const auto [ok, text] = run(store, "GET k");
    EXPECT_TRUE(ok);
    EXPECT_EQ(text, std::string("hello"));
}

TEST(Executor, GetMissingKeyIsNotOk) {
    Store store;
    EXPECT_FALSE(run(store, "GET absent").ok);
}

TEST(Executor, ExistsReportsPresence) {
    Store store;
    EXPECT_EQ(run(store, "EXISTS k").text, std::string("0"));
    run(store, "SET k v");
    EXPECT_EQ(run(store, "EXISTS k").text, std::string("1"));
}

TEST(Executor, TypeReportsAlternative) {
    Store store;
    run(store, "SET s text");
    run(store, "INCR n 5");
    EXPECT_EQ(run(store, "TYPE s").text, std::string("string"));
    EXPECT_EQ(run(store, "TYPE n").text, std::string("int"));
}

TEST(Executor, IncrCreatesAndAccumulates) {
    Store store;
    EXPECT_EQ(run(store, "INCR n").text, std::string("1"));
    EXPECT_EQ(run(store, "INCR n 41").text, std::string("42"));
}

TEST(Executor, IncrOnStringFailsWithoutThrowing) {
    Store store;
    run(store, "SET s text");
    const auto [ok, text] = run(store, "INCR s");
    EXPECT_FALSE(ok);
}

TEST(Executor, QuotedValueKeepsSpaces) {
    Store store;
    run(store, R"(SET greeting "hello world")");
    EXPECT_EQ(run(store, "GET greeting").text, std::string("hello world"));
}
