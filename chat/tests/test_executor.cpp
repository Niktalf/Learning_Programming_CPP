#include <gtest/gtest.h>

#include <exception>
#include <string>

#include "chat/chat_store.hpp"
#include "chat/errors.hpp"
#include "chat/executor.hpp"
#include "chat/parser.hpp"

using chat::ChatStore;
using chat::Response;
using chat::Session;

namespace {

// Исполняет одну строку от имени участника, без границы: ошибка вылетает
// наружу исключением. Строка живёт внутри функции, ответ — независимая
// копия, поэтому его можно возвращать.
Response run(ChatStore& store, Session& session, const std::string& text) {
    const chat::InputLine line{text};
    return chat::execute(store, session, chat::parse(line));
}

// Исходная причина исключения нужного типа или nullptr. Исполнитель
// оборачивает каждую ошибку звеном контекста, поэтому тип проверяется по
// корню цепочки, а не по внешнему исключению.
template <class E>
bool root_is(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(chat::root_cause(error));
    } catch (const E&) {
        return true;
    } catch (...) {
        return false;
    }
}

template <class E>
bool fails_with(ChatStore& store, Session& session, const std::string& text) {
    try {
        static_cast<void>(run(store, session, text));
    } catch (...) {
        return root_is<E>(std::current_exception());
    }
    return false;
}

}  // namespace

TEST(Executor, JoinMessageHistory) {
    ChatStore store;
    Session alice{"alice"};

    EXPECT_TRUE(run(store, alice, "/join #general").ok);
    EXPECT_TRUE(run(store, alice, "/msg #general привет всем").ok);
    EXPECT_EQ(run(store, alice, "/history #general").text,
              std::string("0: <alice> * alice joined\n1: <alice> привет всем"));
}

TEST(Executor, MessageRequiresMembership) {
    ChatStore store;
    Session alice{"alice"};
    Session bob{"bob"};
    run(store, alice, "/join #general");

    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, bob, "/msg #general hi"));
    EXPECT_TRUE(fails_with<chat::UnknownRoomError>(store, bob, "/msg #absent hi"));
}

TEST(Executor, WhoListsMembersInOrder) {
    ChatStore store;
    Session carol{"carol"};
    Session alice{"alice"};
    Session bob{"bob"};
    run(store, carol, "/join #general");
    run(store, alice, "/join #general");
    run(store, bob, "/join #general");
    run(store, bob, "/leave #general");

    EXPECT_EQ(run(store, alice, "/who #general").text, std::string("alice\ncarol"));
    EXPECT_TRUE(fails_with<chat::UnknownRoomError>(store, alice, "/who #absent"));
}

TEST(Executor, LeaveWithoutJoinIsAnError) {
    ChatStore store;
    Session alice{"alice"};
    Session bob{"bob"};
    run(store, alice, "/join #general");
    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, bob, "/leave #general"));
}

TEST(Executor, TypeReportsAlternative) {
    ChatStore store;
    Session alice{"alice"};
    run(store, alice, "/join #general");
    run(store, alice, "/msg #general hello");
    run(store, alice, "/msg #general :thumbsup:");

    EXPECT_EQ(run(store, alice, "/type #general 0").text, std::string("notice"));
    EXPECT_EQ(run(store, alice, "/type #general 1").text, std::string("text"));
    EXPECT_EQ(run(store, alice, "/type #general 2").text, std::string("reaction"));
    EXPECT_TRUE(fails_with<chat::MessageNotFoundError>(store, alice, "/type #general 3"));
}

TEST(Executor, ReactCountsOnlyReactions) {
    ChatStore store;
    Session alice{"alice"};
    run(store, alice, "/join #general");
    run(store, alice, "/msg #general hello");
    run(store, alice, "/msg #general :thumbsup:");

    EXPECT_EQ(run(store, alice, "/react #general 2").text, std::string("2"));
    EXPECT_EQ(run(store, alice, "/react #general 2 5").text, std::string("7"));
    EXPECT_TRUE(fails_with<chat::TypeMismatchError>(store, alice, "/react #general 1"));
}

TEST(Executor, ReactOverflowIsAnErrorNotUndefinedBehaviour) {
    ChatStore store;
    Session alice{"alice"};
    run(store, alice, "/join #general");
    run(store, alice, "/msg #general :fire:");   // счётчик 1

    EXPECT_EQ(run(store, alice, "/react #general 1 9223372036854775806").text,
              std::string("9223372036854775807"));
    EXPECT_TRUE(fails_with<chat::CounterOverflowError>(store, alice, "/react #general 1"));
    // Бросок не испортил счётчик.
    EXPECT_EQ(run(store, alice, "/react #general 1 0").text, std::string("9223372036854775807"));
}

TEST(Executor, TopicShowAndSet) {
    ChatStore store;
    Session alice{"alice"};
    Session bob{"bob"};
    run(store, alice, "/join #general");

    EXPECT_EQ(run(store, alice, "/topic #general").text, std::string("(no topic)"));
    EXPECT_TRUE(run(store, alice, R"(/topic #general "обсуждаем курс")").ok);
    EXPECT_EQ(run(store, alice, "/topic #general").text, std::string("обсуждаем курс"));
    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, bob, R"(/topic #general "чужая")"));
}

TEST(Executor, HistoryRespectsLimit) {
    ChatStore store;
    Session alice{"alice"};
    run(store, alice, "/join #general");
    for (int i = 0; i < 5; ++i) {
        run(store, alice, "/msg #general m" + std::to_string(i));
    }
    EXPECT_EQ(run(store, alice, "/history #general 2").text,
              std::string("4: <alice> m3\n5: <alice> m4"));
    EXPECT_TRUE(fails_with<chat::UnknownRoomError>(store, alice, "/history #absent"));
}

TEST(Executor, MsgToSeveralRoomsReachesAll) {
    ChatStore store;
    Session alice{"alice"};
    run(store, alice, "/join #a");
    run(store, alice, "/join #b");

    EXPECT_TRUE(run(store, alice, "/msg #a,#b hello both").ok);
    EXPECT_EQ(run(store, alice, "/history #a 1").text, std::string("1: <alice> hello both"));
    EXPECT_EQ(run(store, alice, "/history #b 1").text, std::string("1: <alice> hello both"));
}

TEST(Executor, MsgFailingInOneRoomPostsNowhere) {
    ChatStore store;
    Session alice{"alice"};
    Session bob{"bob"};
    run(store, alice, "/join #a");
    run(store, alice, "/join #b");
    run(store, bob, "/join #c");   // alice в #c не состоит

    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, alice, "/msg #a,#c,#b hello"));
    // Ни в одной из комнат сообщения нет — даже в #a, стоящей первой.
    EXPECT_EQ(run(store, alice, "/history #a").text, std::string("0: <alice> * alice joined"));
    EXPECT_EQ(run(store, alice, "/history #b").text, std::string("0: <alice> * alice joined"));
}

TEST(Executor, NickChangesSessionAndMembership) {
    ChatStore store;
    store.register_nick("alice");
    store.register_nick("bob");
    Session alice{"alice"};
    run(store, alice, "/join #general");

    EXPECT_EQ(run(store, alice, "/nick alicia").text, std::string("OK, you are alicia"));
    EXPECT_EQ(alice.nick, std::string("alicia"));
    EXPECT_EQ(run(store, alice, "/who #general").text, std::string("alicia"));

    EXPECT_TRUE(fails_with<chat::NicknameTakenError>(store, alice, "/nick bob"));
    EXPECT_EQ(alice.nick, std::string("alicia"));   // бросок оставил ник прежним
}

TEST(Executor, ErrorCarriesCommandContext) {
    ChatStore store;
    Session alice{"alice"};
    try {
        static_cast<void>(run(store, alice, "/who #absent"));
        ADD_FAILURE() << "must throw";
    } catch (const chat::ChatError& e) {
        // Внешнее звено — контекст, внутреннее — причина.
        EXPECT_EQ(std::string(e.what()), std::string("while executing /who #absent"));
        EXPECT_TRUE(root_is<chat::UnknownRoomError>(std::current_exception()));
    }
}
