#include <gtest/gtest.h>

#include <string>

#include "chat/chat_store.hpp"
#include "chat/executor.hpp"
#include "chat/parser.hpp"

using chat::ChatStore;
using chat::Response;
using chat::Session;

namespace {

// Исполняет одну строку от имени участника. Строка живёт внутри функции,
// ответ — независимая копия, поэтому её можно возвращать.
Response run(ChatStore& store, const Session& session, const std::string& text) {
    const chat::InputLine line{text};
    if (const auto command = chat::parse(line); command) {
        return chat::execute(store, session, *command);
    }
    return Response{false, "ERR parse error"};
}

}  // namespace

TEST(Executor, JoinMessageHistory) {
    ChatStore store;
    const Session alice{"alice"};

    EXPECT_TRUE(run(store, alice, "/join #general").ok);
    EXPECT_TRUE(run(store, alice, "/msg #general привет всем").ok);

    const Response history = run(store, alice, "/history #general");
    EXPECT_TRUE(history.ok);
    EXPECT_EQ(history.text, std::string("0: <alice> * alice joined\n1: <alice> привет всем"));
}

TEST(Executor, MessageRequiresMembership) {
    ChatStore store;
    const Session alice{"alice"};
    const Session bob{"bob"};
    run(store, alice, "/join #general");

    const Response refused = run(store, bob, "/msg #general hi");
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.text, std::string("ERR you are not in #general (use /join)"));
}

TEST(Executor, WhoListsMembersInOrder) {
    ChatStore store;
    run(store, Session{"carol"}, "/join #general");
    run(store, Session{"alice"}, "/join #general");
    run(store, Session{"bob"}, "/join #general");
    run(store, Session{"bob"}, "/leave #general");

    EXPECT_EQ(run(store, Session{"alice"}, "/who #general").text, std::string("alice\ncarol"));
    EXPECT_FALSE(run(store, Session{"alice"}, "/who #absent").ok);
}

TEST(Executor, LeaveWithoutJoinIsAnError) {
    ChatStore store;
    run(store, Session{"alice"}, "/join #general");
    EXPECT_FALSE(run(store, Session{"bob"}, "/leave #general").ok);
}

TEST(Executor, TypeReportsAlternative) {
    ChatStore store;
    const Session alice{"alice"};
    run(store, alice, "/join #general");
    run(store, alice, "/msg #general hello");
    run(store, alice, "/msg #general :thumbsup:");

    EXPECT_EQ(run(store, alice, "/type #general 0").text, std::string("notice"));
    EXPECT_EQ(run(store, alice, "/type #general 1").text, std::string("text"));
    EXPECT_EQ(run(store, alice, "/type #general 2").text, std::string("reaction"));
    EXPECT_FALSE(run(store, alice, "/type #general 3").ok);
}

TEST(Executor, ReactCountsOnlyReactions) {
    ChatStore store;
    const Session alice{"alice"};
    run(store, alice, "/join #general");
    run(store, alice, "/msg #general hello");
    run(store, alice, "/msg #general :thumbsup:");

    EXPECT_EQ(run(store, alice, "/react #general 2").text, std::string("2"));
    EXPECT_EQ(run(store, alice, "/react #general 2 5").text, std::string("7"));

    const Response wrong = run(store, alice, "/react #general 1");
    EXPECT_FALSE(wrong.ok);
    EXPECT_EQ(wrong.text, std::string("ERR REACT needs a reaction, the message is text"));
}

TEST(Executor, TopicShowAndSet) {
    ChatStore store;
    const Session alice{"alice"};
    run(store, alice, "/join #general");

    EXPECT_EQ(run(store, alice, "/topic #general").text, std::string("(no topic)"));
    EXPECT_TRUE(run(store, alice, R"(/topic #general "обсуждаем курс")").ok);
    EXPECT_EQ(run(store, alice, "/topic #general").text, std::string("обсуждаем курс"));
    EXPECT_FALSE(run(store, Session{"bob"}, R"(/topic #general "чужая")").ok);
}

TEST(Executor, HistoryRespectsLimit) {
    ChatStore store;
    const Session alice{"alice"};
    run(store, alice, "/join #general");
    for (int i = 0; i < 5; ++i) {
        run(store, alice, "/msg #general m" + std::to_string(i));
    }
    EXPECT_EQ(run(store, alice, "/history #general 2").text,
              std::string("4: <alice> m3\n5: <alice> m4"));
    EXPECT_FALSE(run(store, alice, "/history #absent").ok);
}
