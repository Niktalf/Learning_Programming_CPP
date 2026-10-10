#include <gtest/gtest.h>

#include <string>

#include "handler.hpp"

namespace {
    bool starts_with(const std::string& s, const std::string& prefix) {
        return s.compare(0, prefix.size(), prefix) == 0;
    }
}  // namespace

TEST(Handler, SuccessPassesThrough) {
    ChatStore store;
    EXPECT_TRUE(handle(store, "/join #general").ok);
}

TEST(Handler, ParseErrorBecomesResponse) {
    ChatStore store;
    const auto r = handle(store, "/flood");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(starts_with(r.text, "ERR unknown command"));
}

TEST(Handler, MessageToMissingRoomBecomesResponse) {
    ChatStore store;
    const auto r = handle(store, "/msg #absent hello");

    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text, std::string("ERR no such room: #absent"));
}

TEST(Handler, TakenNicknameBecomesResponse) {
    ChatStore store;
    (void)handle(store, "/nick alice");
    const auto [ok, text] = handle(store, "/nick alice");

    EXPECT_FALSE(ok);
    EXPECT_EQ(text, std::string("ERR nickname in use: alice"));
}

TEST(Handler, ReactOnTextBecomesTypeMismatch) {
    ChatStore store;
    (void)handle(store, "/join #general");
    (void)handle(store, "/msg #general text");
    const auto [ok, text] = handle(store, "/react #general 0");

    EXPECT_FALSE(ok);
    EXPECT_EQ(text, std::string("ERR wrong type: expected reaction, got text"));
}

TEST(Handler, UnbalancedQuoteInMessageIsNotAnError) {
    ChatStore store;
    (void)handle(store, "/join #general");

    EXPECT_TRUE(handle(store, R"(/msg #general he said "hi)").ok);
}

TEST(Handler, NeverThrowsOnGarbage) {
    ChatStore store;

    EXPECT_NO_THROW((void)handle(store, ""));
    EXPECT_NO_THROW((void)handle(store, "/msg"));
    EXPECT_NO_THROW((void)handle(store, "/history #x 99999999999999999999999"));
    EXPECT_NO_THROW((void)handle(store, "/topic \"open"));
}

TEST(Handler, FailedMultiRoomMessageDoesNotPartiallyDeliver) {
    ChatStore store;
    (void)handle(store, "/join #a");
    (void)handle(store, "/join #b");

    EXPECT_FALSE(handle(store, "/msg #a #b #absent to all").ok);
    EXPECT_EQ(handle(store, "/history #a").text, std::string("(empty)\n"));
    EXPECT_EQ(handle(store, "/history #b").text, std::string("(empty)\n"));
}
