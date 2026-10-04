#include <gtest/gtest.h>

#include <string>

#include "handler.hpp"

namespace {
    bool starts_with(const std::string& s, const std::string& prefix) {
        return s.compare(0, prefix.size(), prefix) == 0;
    }
}  // namespace

TEST(Handler, SuccessPassesThrough) {
    Store store;
    const auto r = handle(store, "SET k v");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.text, std::string("OK"));
}

TEST(Handler, ParseErrorBecomesResponse) {
    Store store;
    const auto [ok, text] = handle(store, "FLUSHALL");
    EXPECT_FALSE(ok);
    EXPECT_TRUE(starts_with(text, "ERR unknown command"));
}

TEST(Handler, UnterminatedQuoteBecomesResponse) {
    Store store;
    const auto [ok, text] = handle(store, R"(SET k "open)");
    EXPECT_FALSE(ok);
    EXPECT_EQ(text, std::string("ERR unterminated quote"));
}

TEST(Handler, MissingKeyBecomesResponse) {
    Store store;
    const auto r = handle(store, "GET absent");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text, std::string("ERR no such key: absent"));
}

TEST(Handler, TypeMismatchBecomesResponse) {
    Store store;
    // handle помечена [[nodiscard]]: выброшенный ответ клиенту почти всегда
    // ошибка. Здесь вызов нужен только для подготовки, и это сказано явно.
    (void)handle(store, "SET s text");
    const auto r = handle(store, "INCR s");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text, std::string("ERR wrong type: expected int, got string"));
}

TEST(Handler, NeverThrowsOnGarbage) {
    Store store;

    EXPECT_NO_THROW(handle(store, ""));
    EXPECT_NO_THROW(handle(store, "\"\"\""));
    EXPECT_NO_THROW(handle(store, "INCR k 99999999999999999999999"));
    EXPECT_NO_THROW(handle(store, "MSET a"));
}

TEST(Handler, FailedCommandDoesNotCorruptState) {
    Store store;
    (void)handle(store, "SET k v");
    (void)handle(store, "MSET a 1 b");
    EXPECT_EQ(store.size(), 1u);
    EXPECT_EQ(handle(store, "GET k").text, std::string("v"));
}
