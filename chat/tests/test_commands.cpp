#include <gtest/gtest.h>

#include <exception>
#include <string>

#include "chat/chat_store.hpp"
#include "chat/commands.hpp"
#include "chat/errors.hpp"

// Команды чата через реестр этапа 6. До него эти тесты назывались
// test_executor и шли через parse и execute; поведение команд то же. Тесты
// разбора из бывшего test_parser — в конце файла.

using chat::ChatStore;
using chat::Response;
using chat::Session;

namespace {

// Исполняет одну строку от имени участника, без границы: ошибка вылетает
// наружу исключением. Строка живёт всё время вызова, ответ — независимая
// копия, поэтому его можно возвращать.
Response run(ChatStore& store, Session& session, const std::string& text) {
    return chat::execute(store, session, text);
}

// Исходная причина исключения нужного типа или nullptr. Связанная команда
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

// Этап 4: в комнату входит пользователь из реестра, поэтому каждая сессия
// начинается с регистрации ника.
Session login(ChatStore& store, const std::string& nick) {
    store.register_user(nick);
    return Session{nick};
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

TEST(Commands, JoinMessageHistory) {
    ChatStore store;
    Session alice = login(store, "alice");

    EXPECT_TRUE(run(store, alice, "/join #general").ok);
    EXPECT_TRUE(run(store, alice, "/msg #general привет всем").ok);
    EXPECT_EQ(run(store, alice, "/history #general").text,
              std::string("0: <alice> * alice joined\n1: <alice> привет всем"));
}

TEST(Commands, MessageRequiresMembership) {
    ChatStore store;
    Session alice = login(store, "alice");
    Session bob = login(store, "bob");
    run(store, alice, "/join #general");

    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, bob, "/msg #general hi"));
    EXPECT_TRUE(fails_with<chat::UnknownRoomError>(store, bob, "/msg #absent hi"));
}

TEST(Commands, WhoListsMembersInOrder) {
    ChatStore store;
    Session carol = login(store, "carol");
    Session alice = login(store, "alice");
    Session bob = login(store, "bob");
    run(store, carol, "/join #general");
    run(store, alice, "/join #general");
    run(store, bob, "/join #general");
    run(store, bob, "/leave #general");

    EXPECT_EQ(run(store, alice, "/who #general").text, std::string("alice\ncarol"));
    EXPECT_TRUE(fails_with<chat::UnknownRoomError>(store, alice, "/who #absent"));
}

TEST(Commands, LeaveWithoutJoinIsAnError) {
    ChatStore store;
    Session alice = login(store, "alice");
    Session bob = login(store, "bob");
    run(store, alice, "/join #general");
    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, bob, "/leave #general"));
}

TEST(Commands, TypeReportsAlternative) {
    ChatStore store;
    Session alice = login(store, "alice");
    run(store, alice, "/join #general");
    run(store, alice, "/msg #general hello");
    run(store, alice, "/msg #general :thumbsup:");

    EXPECT_EQ(run(store, alice, "/type #general 0").text, std::string("notice"));
    EXPECT_EQ(run(store, alice, "/type #general 1").text, std::string("text"));
    EXPECT_EQ(run(store, alice, "/type #general 2").text, std::string("reaction"));
    EXPECT_TRUE(fails_with<chat::MessageNotFoundError>(store, alice, "/type #general 3"));
}

TEST(Commands, ReactCountsOnlyReactions) {
    ChatStore store;
    Session alice = login(store, "alice");
    run(store, alice, "/join #general");
    run(store, alice, "/msg #general hello");
    run(store, alice, "/msg #general :thumbsup:");

    EXPECT_EQ(run(store, alice, "/react #general 2").text, std::string("2"));
    EXPECT_EQ(run(store, alice, "/react #general 2 5").text, std::string("7"));
    EXPECT_TRUE(fails_with<chat::TypeMismatchError>(store, alice, "/react #general 1"));
}

TEST(Commands, ReactOverflowIsAnErrorNotUndefinedBehaviour) {
    ChatStore store;
    Session alice = login(store, "alice");
    run(store, alice, "/join #general");
    run(store, alice, "/msg #general :fire:");   // счётчик 1

    EXPECT_EQ(run(store, alice, "/react #general 1 9223372036854775806").text,
              std::string("9223372036854775807"));
    EXPECT_TRUE(fails_with<chat::CounterOverflowError>(store, alice, "/react #general 1"));
    // Бросок не испортил счётчик.
    EXPECT_EQ(run(store, alice, "/react #general 1 0").text, std::string("9223372036854775807"));
}

TEST(Commands, TopicShowAndSet) {
    ChatStore store;
    Session alice = login(store, "alice");
    Session bob = login(store, "bob");
    run(store, alice, "/join #general");

    EXPECT_EQ(run(store, alice, "/topic #general").text, std::string("(no topic)"));
    EXPECT_TRUE(run(store, alice, R"(/topic #general "обсуждаем курс")").ok);
    EXPECT_EQ(run(store, alice, "/topic #general").text, std::string("обсуждаем курс"));
    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, bob, R"(/topic #general "чужая")"));
}

TEST(Commands, HistoryRespectsLimit) {
    ChatStore store;
    Session alice = login(store, "alice");
    run(store, alice, "/join #general");
    for (int i = 0; i < 5; ++i) {
        run(store, alice, "/msg #general m" + std::to_string(i));
    }
    EXPECT_EQ(run(store, alice, "/history #general 2").text,
              std::string("4: <alice> m3\n5: <alice> m4"));
    EXPECT_TRUE(fails_with<chat::UnknownRoomError>(store, alice, "/history #absent"));
}

TEST(Commands, MsgToSeveralRoomsReachesAll) {
    ChatStore store;
    Session alice = login(store, "alice");
    run(store, alice, "/join #a");
    run(store, alice, "/join #b");

    EXPECT_TRUE(run(store, alice, "/msg #a,#b hello both").ok);
    EXPECT_EQ(run(store, alice, "/history #a 1").text, std::string("1: <alice> hello both"));
    EXPECT_EQ(run(store, alice, "/history #b 1").text, std::string("1: <alice> hello both"));
}

TEST(Commands, MsgFailingInOneRoomPostsNowhere) {
    ChatStore store;
    Session alice = login(store, "alice");
    Session bob = login(store, "bob");
    run(store, alice, "/join #a");
    run(store, alice, "/join #b");
    run(store, bob, "/join #c");   // alice в #c не состоит

    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, alice, "/msg #a,#c,#b hello"));
    // Ни в одной из комнат сообщения нет — даже в #a, стоящей первой.
    EXPECT_EQ(run(store, alice, "/history #a").text, std::string("0: <alice> * alice joined"));
    EXPECT_EQ(run(store, alice, "/history #b").text, std::string("0: <alice> * alice joined"));
}

TEST(Commands, NickChangesSessionAndMembership) {
    ChatStore store;
    Session alice = login(store, "alice");
    login(store, "bob");   // ник bob занят
    run(store, alice, "/join #general");

    EXPECT_EQ(run(store, alice, "/nick alicia").text, std::string("OK, you are alicia"));
    EXPECT_EQ(alice.nick, std::string("alicia"));
    EXPECT_EQ(run(store, alice, "/who #general").text, std::string("alicia"));

    EXPECT_TRUE(fails_with<chat::NicknameTakenError>(store, alice, "/nick bob"));
    EXPECT_EQ(alice.nick, std::string("alicia"));   // бросок оставил ник прежним
}

TEST(Commands, ErrorCarriesCommandContext) {
    ChatStore store;
    Session alice = login(store, "alice");
    try {
        static_cast<void>(run(store, alice, "/who #absent"));
        ADD_FAILURE() << "must throw";
    } catch (const chat::ChatError& e) {
        // Внешнее звено — контекст, внутреннее — причина.
        EXPECT_EQ(std::string(e.what()), std::string("while executing /who #absent"));
        EXPECT_TRUE(root_is<chat::UnknownRoomError>(std::current_exception()));
    }
}

TEST(Commands, UnregisteredUserCannotJoin) {
    ChatStore store;
    Session ghost{"ghost"};   // в реестре его нет
    EXPECT_TRUE(fails_with<chat::UnknownUserError>(store, ghost, "/join #general"));
}

TEST(Commands, DepartedUserDisappearsFromWho) {
    ChatStore store;
    Session alice = login(store, "alice");
    Session bob = login(store, "bob");
    run(store, alice, "/join #general");
    run(store, bob, "/join #general");

    store.release_user("bob");   // на этапе 15 — отключение клиента
    EXPECT_EQ(run(store, alice, "/who #general").text, std::string("alice"));
    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, bob, "/msg #general hi"));
}

// --- Разбор: бывший test_parser ------------------------------------------

namespace {

// Причина отказа разбора — по корню цепочки: ошибку аргумента реестр
// оборачивает подсказкой «usage: …». Пустая строка — разбор прошёл.
std::string reason_for(const std::string& text) {
    try {
        static_cast<void>(chat::commands().bind(text));
    } catch (...) {
        try {
            std::rethrow_exception(chat::root_cause(std::current_exception()));
        } catch (const chat::ParseError& e) {
            return e.reason();
        } catch (...) {
            return "(not a parse error)";
        }
    }
    return {};
}

// Подсказка, приклеенная к ошибке аргумента, или пустая строка.
std::string usage_for(const std::string& text) {
    try {
        static_cast<void>(chat::commands().bind(text));
    } catch (...) {
        const auto context = chat::context_of(std::current_exception());
        return context.empty() ? std::string() : context.front();
    }
    return {};
}

}  // namespace

TEST(Commands, MsgTakesRestOfLineAsText) {
    ChatStore store;
    Session alice = login(store, "alice");
    run(store, alice, "/join #general");
    run(store, alice, "/msg #general всем привет,   как дела");
    EXPECT_EQ(run(store, alice, "/history #general 1").text,
              std::string("1: <alice> всем привет,   как дела"));
}

TEST(Commands, MsgTextMayContainQuotes) {
    // Tail — остаток строки как есть: кавычки внутри не разбираются, и
    // незакрытая кавычка в тексте — не ошибка.
    ChatStore store;
    Session alice = login(store, "alice");
    run(store, alice, "/join #general");
    run(store, alice, R"(/msg #general он сказал "привет)");
    EXPECT_EQ(run(store, alice, "/history #general 1").text,
              std::string(R"(1: <alice> он сказал "привет)"));
}

TEST(Commands, VerbIsCaseInsensitive) {
    ChatStore store;
    Session alice = login(store, "alice");
    EXPECT_TRUE(run(store, alice, "/JOIN #general").ok);
    EXPECT_EQ(run(store, alice, "/Who #general").text, std::string("alice"));
}

TEST(Commands, RejectsMalformedInput) {
    for (const char* const text :
         {"", "   ", "/msg", "/msg #general", "/join general", "/join #", "/history #general x",
          "/history #general 3x", "/react #general", "/type #general -1", "/unknown #general",
          R"(/topic #general "незакрыто)", "hello", "/rooms extra"}) {
        EXPECT_NE(reason_for(text), std::string()) << text;
    }
}

TEST(Commands, ErrorCarriesReason) {
    EXPECT_EQ(reason_for("/join general"), std::string("room name must start with #: general"));
    EXPECT_EQ(reason_for("/frob #general"), std::string("unknown command: /frob"));
    EXPECT_EQ(reason_for("/history #general x"), std::string("not a non-negative number: x"));
    EXPECT_EQ(reason_for("/type #general -1"), std::string("not a non-negative number: -1"));
    EXPECT_EQ(reason_for("/react #general 1 x"), std::string("not a number: x"));
}

TEST(Commands, ArityErrorIsUsageFromTheSignature) {
    // Ни один обработчик арность не проверяет: подсказку реестр собирает из
    // типов параметров.
    EXPECT_EQ(reason_for("/join #a #b"), std::string("usage: /join <#room>"));
    EXPECT_EQ(reason_for("/msg #general"), std::string("usage: /msg <#room[,#room...]> <text...>"));
    EXPECT_EQ(reason_for("/history"), std::string("usage: /history <#room> [number] [nick]"));
    EXPECT_EQ(reason_for("/react #a"), std::string("usage: /react <#room> <number> [number]"));
    EXPECT_EQ(reason_for("/nick a b"), std::string("usage: /nick <nick>"));
    EXPECT_EQ(reason_for("/rooms extra"), std::string("usage: /rooms"));
    // Многословная тема без кавычек — больше токенов, чем у /topic параметров.
    EXPECT_EQ(reason_for("/topic #general обсуждаем курс"),
              std::string(R"(usage: /topic <#room> ["text"])"));
}

TEST(Commands, ArgumentErrorGetsUsageAttached) {
    EXPECT_EQ(usage_for("/history #general x"), std::string("usage: /history <#room> [number] [nick]"));
    EXPECT_EQ(usage_for("/join general"), std::string("usage: /join <#room>"));
}

TEST(Commands, RoomListErrors) {
    EXPECT_EQ(reason_for("/msg #a,#a hi"), std::string("room listed twice: #a"));
    EXPECT_EQ(reason_for("/msg #a,,#b hi"), std::string("room name must start with #: "));
    EXPECT_EQ(reason_for("/msg #a,b hi"), std::string("room name must start with #: b"));
}

TEST(Commands, NickIsValidatedByItsType) {
    EXPECT_EQ(reason_for(R"(/nick "   ")"),
              std::string("nickname may contain only letters, digits, _ and -"));
    EXPECT_EQ(reason_for("/history #general 3 \"no way\""),
              std::string("nickname may contain only letters, digits, _ and -"));
}

TEST(Commands, HistoryOfOneAuthor) {
    ChatStore store;
    Session alice = login(store, "alice");
    Session bob = login(store, "bob");
    run(store, alice, "/join #general");
    run(store, bob, "/join #general");
    run(store, alice, "/msg #general a1");
    run(store, bob, "/msg #general b1");
    EXPECT_EQ(run(store, alice, "/history #general 5 bob").text,
              std::string("1: <bob> * bob joined\n3: <bob> b1"));
}

// --- Домашнее задание этапа 6 -------------------------------------------

TEST(Commands, KickRemovesMemberAndLeavesNotice) {
    ChatStore store;
    Session alice = login(store, "alice");
    Session bob = login(store, "bob");
    run(store, alice, "/join #general");
    run(store, bob, "/join #general");

    EXPECT_EQ(run(store, alice, "/kick #general bob").text, std::string("OK"));
    EXPECT_EQ(run(store, alice, "/who #general").text, std::string("alice"));
    EXPECT_EQ(run(store, alice, "/history #general 1").text,
              std::string("2: <alice> * alice kicked bob"));
    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, bob, "/msg #general hi"));
}

TEST(Commands, KickRules) {
    ChatStore store;
    Session alice = login(store, "alice");
    Session bob = login(store, "bob");
    Session carol = login(store, "carol");
    run(store, alice, "/join #general");
    run(store, bob, "/join #general");

    // Исключать может только участник, и только участника.
    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, carol, "/kick #general bob"));
    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, alice, "/kick #general carol"));
    EXPECT_TRUE(fails_with<chat::NotMemberError>(store, alice, "/kick #general nobody"));
    EXPECT_TRUE(fails_with<chat::UnknownRoomError>(store, alice, "/kick #absent bob"));
    EXPECT_TRUE(fails_with<chat::ChatError>(store, alice, "/kick #general alice"));
    EXPECT_EQ(run(store, alice, "/who #general").text, std::string("alice\nbob"));
}

TEST(Commands, RoomsAreListedAlphabetically) {
    ChatStore store;
    Session alice = login(store, "alice");
    Session bob = login(store, "bob");
    EXPECT_EQ(run(store, alice, "/rooms").text, std::string("(no rooms)"));
    run(store, alice, "/join #zeta");
    run(store, alice, "/join #alpha");
    run(store, bob, "/join #alpha");
    run(store, alice, "/leave #zeta");
    EXPECT_EQ(run(store, alice, "/rooms").text, std::string("#alpha (2)\n#zeta (0)"));
}
