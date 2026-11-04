#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "kvs/executor.hpp"
#include "kvs/parser.hpp"

using kvs::InputLine;
using kvs::Response;
using kvs::Store;

namespace {

Response run(Store& store, std::string text) {
    const InputLine line{std::move(text)};
    const auto command = kvs::parse(line);
    if (!command) {
        return Response{false, "ERR parse error"};
    }
    return kvs::execute(store, *command);
}

}  // namespace

TEST(Executor, SetThenGet) {
    Store store;
    EXPECT_TRUE(run(store, "SET k hello").ok);
    const auto got = run(store, "GET k");
    EXPECT_TRUE(got.ok);
    EXPECT_EQ(got.text, std::string("hello"));
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
    const auto response = run(store, "INCR s");
    // Ключевое: неверная альтернатива не роняет обработку команды.
    // На этапе 15 такое исключение оборвало бы соединение клиента.
    EXPECT_FALSE(response.ok);
}

TEST(Executor, QuotedValueKeepsSpaces) {
    Store store;
    run(store, R"(SET greeting "hello world")");
    EXPECT_EQ(run(store, "GET greeting").text, std::string("hello world"));
}
