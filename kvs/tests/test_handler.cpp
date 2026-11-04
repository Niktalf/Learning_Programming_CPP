#include <gtest/gtest.h>

#include <string>

#include "kvs/handler.hpp"

using kvs::Store;
using kvs::handle;

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
    const auto r = handle(store, "FLUSHALL");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(starts_with(r.text, "ERR unknown command"));
}

TEST(Handler, UnterminatedQuoteBecomesResponse) {
    Store store;
    const auto r = handle(store, R"(SET k "open)");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text, std::string("ERR unterminated quote"));
}

TEST(Handler, MissingKeyBecomesResponse) {
    Store store;
    const auto r = handle(store, "GET absent");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text, std::string("ERR no such key: absent (while executing GET absent)"));
}

TEST(Handler, TypeMismatchBecomesResponse) {
    Store store;
    // handle помечена [[nodiscard]]: выброшенный ответ клиенту почти всегда
    // ошибка. Здесь вызов нужен только для подготовки, и это сказано явно.
    static_cast<void>(handle(store, "SET s text"));
    const auto r = handle(store, "INCR s");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text,
              std::string("ERR wrong type: expected int, got string (while executing INCR s)"));
}

TEST(Handler, NeverThrowsOnGarbage) {
    Store store;
    // Граница не выпускает наружу ничего — это и есть её назначение.
    EXPECT_NO_THROW(static_cast<void>(handle(store, "")));
    EXPECT_NO_THROW(static_cast<void>(handle(store, "\"\"\"")));
    EXPECT_NO_THROW(static_cast<void>(handle(store, "INCR k 99999999999999999999999")));
    EXPECT_NO_THROW(static_cast<void>(handle(store, "MSET a")));
}

TEST(Handler, FailedCommandDoesNotCorruptState) {
    Store store;
    static_cast<void>(handle(store, "SET k v"));
    static_cast<void>(handle(store, "MSET a 1 b"));   // нечётное число аргументов — отказ разбора
    EXPECT_EQ(store.size(), 1u);
    EXPECT_EQ(handle(store, "GET k").text, std::string("v"));
}

TEST(Handler, MemoryLimitBecomesResponse) {
    Store store(8);
    EXPECT_TRUE(handle(store, "SET k 1234").ok);
    const auto r = handle(store, "MSET a 1 b 1234567");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text, std::string("ERR out of memory: 15 bytes required, limit is 8 "
                                  "(while executing MSET a b)"));
    EXPECT_EQ(store.size(), 1u);   // MSET не записал ни одной пары
}

TEST(Handler, OverflowBecomesResponse) {
    Store store;
    static_cast<void>(handle(store, "INCR n 9223372036854775807"));
    const auto r = handle(store, "INCR n");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text, std::string("ERR increment would overflow (while executing INCR n)"));
}
