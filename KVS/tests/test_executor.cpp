#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "errors.hpp"
#include "executor.hpp"
#include "parser.hpp"


namespace {
    Response run(Store& store, std::string text) {
        const InputLine line{std::move(text)};
        return execute(store, parse(line));
    }
}  // namespace

TEST(Executor, SetThenGet) {
    Store store;

    EXPECT_TRUE(run(store, "SET k hello").ok);

    const auto [ok, text] = run(store, "GET k");

    EXPECT_TRUE(ok);
    EXPECT_EQ(text, std::string("hello"));
}

TEST(Executor, GetMissingKeyThrows) {
    Store store;

    EXPECT_THROW(run(store, "GET absent"), KeyNotFoundError);
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

TEST(Executor, IncrOnStringThrowsTypeMismatch) {
    Store store;
    run(store, "SET s text");

    try {
        run(store, "INCR s");

        EXPECT_TRUE(false);
    } catch (const TypeMismatchError& e) {
        EXPECT_EQ(e.key(), std::string("s"));
        EXPECT_EQ(e.actual(), std::string("string"));
    }
}

TEST(Executor, MsetWritesAllPairs) {
    Store store;
    run(store, "MSET a 1 b 2 c 3");

    EXPECT_EQ(store.size(), 3u);
    EXPECT_EQ(run(store, "GET b").text, std::string("2"));
}

TEST(Executor, QuotedValueKeepsSpaces) {
    Store store;
    run(store, R"(SET greeting "hello world")");
    
    EXPECT_EQ(run(store, "GET greeting").text, std::string("hello world"));
}
