#include <gtest/gtest.h>

#include <string>

#include "chat/chat_store.hpp"
#include "chat/handler.hpp"

using chat::ChatStore;
using chat::handle;
using chat::Session;

namespace {

std::string reply(ChatStore& store, Session& session, const std::string& line) {
    return handle(store, session, line).text;
}

}  // namespace

TEST(Handler, SuccessPassesThrough) {
    ChatStore store;
    Session alice{"alice"};
    EXPECT_EQ(reply(store, alice, "/join #general"), std::string("OK"));
    EXPECT_EQ(reply(store, alice, "/who #general"), std::string("alice"));
}

TEST(Handler, EachErrorBecomesResponse) {
    ChatStore store;
    store.register_nick("alice");
    store.register_nick("bob");
    Session alice{"alice"};
    Session bob{"bob"};
    static_cast<void>(handle(store, alice, "/join #general"));
    static_cast<void>(handle(store, alice, "/msg #general hello"));

    EXPECT_EQ(reply(store, alice, "/frob #general"), std::string("ERR unknown command: /frob"));
    EXPECT_EQ(reply(store, alice, "/who #absent"),
              std::string("ERR no such room: #absent (while executing /who #absent)"));
    EXPECT_EQ(reply(store, bob, "/msg #general hi"),
              std::string("ERR you are not in #general (use /join) "
                          "(while executing /msg #general)"));
    EXPECT_EQ(reply(store, alice, "/nick bob"),
              std::string("ERR nickname in use: bob (while executing /nick bob)"));
    EXPECT_EQ(reply(store, alice, "/type #general 7"),
              std::string("ERR no message 7 in #general (while executing /type #general)"));
    EXPECT_EQ(reply(store, alice, "/react #general 1"),
              std::string("ERR wrong type: expected reaction, got text "
                          "(while executing /react #general)"));
}

TEST(Handler, ResponseIsMarkedFailed) {
    ChatStore store;
    Session alice{"alice"};
    EXPECT_FALSE(handle(store, alice, "/who #absent").ok);
    EXPECT_TRUE(handle(store, alice, "/join #general").ok);
}

TEST(Handler, NeverThrowsOnGarbage) {
    ChatStore store;
    Session alice{"alice"};
    // Граница не выпускает наружу ничего — это и есть её назначение.
    EXPECT_NO_THROW(static_cast<void>(handle(store, alice, "")));
    EXPECT_NO_THROW(static_cast<void>(handle(store, alice, "\"\"\"")));
    EXPECT_NO_THROW(static_cast<void>(handle(store, alice, "/react #general 0 99999999999999999999")));
    EXPECT_NO_THROW(static_cast<void>(handle(store, alice, "/msg ,,, x")));
}

TEST(Handler, FailedCommandDoesNotCorruptState) {
    ChatStore store;
    Session alice{"alice"};
    static_cast<void>(handle(store, alice, "/join #a"));
    static_cast<void>(handle(store, alice, "/msg #a,#absent hello"));   // вторая комнаты нет
    EXPECT_EQ(reply(store, alice, "/history #a"), std::string("0: <alice> * alice joined"));
}
