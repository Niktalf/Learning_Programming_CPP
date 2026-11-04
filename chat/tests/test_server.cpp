#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "chat/errors.hpp"
#include "chat/server.hpp"
#include "temp_dir.hpp"

using chat::Server;
using chat::Session;

namespace {

std::string reply(Server& server, Session& session, const std::string& line) {
    return server.handle(session, line).text;
}

}  // namespace

TEST(Server, SuccessPassesThrough) {
    Server server;
    Session alice = server.login("alice");
    EXPECT_EQ(reply(server, alice, "/join #general"), std::string("OK"));
    EXPECT_EQ(reply(server, alice, "/who #general"), std::string("alice"));
}

TEST(Server, EachErrorBecomesResponse) {
    Server server;
    Session alice = server.login("alice");
    Session bob = server.login("bob");
    static_cast<void>(server.handle(alice, "/join #general"));
    static_cast<void>(server.handle(alice, "/msg #general hello"));

    EXPECT_EQ(reply(server, alice, "/frob #general"), std::string("ERR unknown command: /frob"));
    EXPECT_EQ(reply(server, alice, "/who #absent"),
              std::string("ERR no such room: #absent (while executing /who #absent)"));
    EXPECT_EQ(reply(server, bob, "/msg #general hi"),
              std::string("ERR you are not in #general (use /join) "
                          "(while executing /msg #general)"));
    EXPECT_EQ(reply(server, alice, "/nick bob"),
              std::string("ERR nickname in use: bob (while executing /nick bob)"));
    EXPECT_EQ(reply(server, alice, "/type #general 7"),
              std::string("ERR no message 7 in #general (while executing /type #general)"));
    EXPECT_EQ(reply(server, alice, "/react #general 1"),
              std::string("ERR wrong type: expected reaction, got text "
                          "(while executing /react #general)"));
}

TEST(Server, ResponseIsMarkedFailed) {
    Server server;
    Session alice = server.login("alice");
    EXPECT_FALSE(server.handle(alice, "/who #absent").ok);
    EXPECT_TRUE(server.handle(alice, "/join #general").ok);
}

TEST(Server, NeverThrowsOnGarbage) {
    Server server;
    Session alice = server.login("alice");
    // Граница не выпускает наружу ничего — это и есть её назначение.
    EXPECT_NO_THROW(static_cast<void>(server.handle(alice, "")));
    EXPECT_NO_THROW(static_cast<void>(server.handle(alice, "\"\"\"")));
    EXPECT_NO_THROW(static_cast<void>(server.handle(alice, "/react #general 0 99999999999999999999")));
    EXPECT_NO_THROW(static_cast<void>(server.handle(alice, "/msg ,,, x")));
}

TEST(Server, FailedCommandDoesNotCorruptState) {
    Server server;
    Session alice = server.login("alice");
    static_cast<void>(server.handle(alice, "/join #a"));
    static_cast<void>(server.handle(alice, "/msg #a,#absent hello"));   // вторая комнаты нет
    EXPECT_EQ(reply(server, alice, "/history #a"), std::string("0: <alice> * alice joined"));
}

// --- Этап 4: вход, выход и журнал сообщений --------------------------------

TEST(Server, LoginTakesTheNick) {
    Server server;
    const Session alice = server.login("alice");
    EXPECT_THROW(static_cast<void>(server.login("alice")), chat::NicknameTakenError);
    server.logout(alice);
    EXPECT_NO_THROW(static_cast<void>(server.login("alice")));
}

TEST(Server, LogoutRemovesUserFromRooms) {
    Server server;
    Session alice = server.login("alice");
    Session bob = server.login("bob");
    static_cast<void>(server.handle(alice, "/join #general"));
    static_cast<void>(server.handle(bob, "/join #general"));

    server.logout(bob);
    EXPECT_EQ(reply(server, alice, "/who #general"), std::string("alice"));
}

TEST(Server, JournalRecordsSentMessages) {
    const chat::testing::TempDir dir;
    const auto path = dir.file("chat.log");
    {
        Server server(path);
        Session alice = server.login("alice");
        static_cast<void>(server.handle(alice, "/join #a"));
        static_cast<void>(server.handle(alice, "/join #b"));
        static_cast<void>(server.handle(alice, "/msg #a hello"));
        static_cast<void>(server.handle(alice, "/msg #a,#b to both"));
        static_cast<void>(server.handle(alice, "/msg #absent lost"));   // неудача — не пишется
    }
    std::ifstream in(path);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    EXPECT_EQ(lines, (std::vector<std::string>{"#a\talice\thello", "#a,#b\talice\tto both"}));
}

TEST(Server, FailedConstructionLeaksNothing) {
    const chat::testing::TempDir dir;
    const int before = chat::testing::open_descriptors();
    EXPECT_THROW(Server(dir.file("missing") / "chat.log"), chat::ChatError);
    EXPECT_EQ(chat::testing::open_descriptors(), before);
}
