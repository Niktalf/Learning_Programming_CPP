// Этап 10: доменные события чата — какие команды что публикуют. Без
// брокера: издатель подставной (FakePublisher).

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

#include "chat/events.hpp"
#include "chat/server.hpp"
#include "fake_publisher.hpp"
#include "memory_log.hpp"

using chat::DomainEvent;
using chat::MessagePosted;
using chat::Server;
using chat::Session;
using chat::UserJoined;
using chat::UserLeft;
using chat::testing::FakePublisher;

namespace {

struct Fixture {
    FakePublisher publisher;
    Server server{chat::ChatStore(), nullptr, publisher.port()};
    Session alice = server.login("alice");
    Session bob = server.login("bob");

    [[nodiscard]] const std::vector<DomainEvent>& events() const { return publisher.events(); }
    bool run(Session& who, const std::string& line) { return server.handle(who, line).ok; }
};

[[nodiscard]] const MessagePosted& posted(const DomainEvent& event) { return std::get<MessagePosted>(event); }

}  // namespace

TEST(Events, JoinPublishesUserJoinedThenNotice) {
    Fixture f;
    ASSERT_TRUE(f.run(f.alice, "/join #general"));
    ASSERT_EQ(f.events().size(), 2u);
    const auto& joined = std::get<UserJoined>(f.events()[0]);
    EXPECT_EQ(joined.room, "#general");
    EXPECT_EQ(joined.nick, "alice");
    EXPECT_EQ(posted(f.events()[1]).kind, "notice");
}

TEST(Events, RepeatedJoinPublishesNothing) {
    Fixture f;
    ASSERT_TRUE(f.run(f.alice, "/join #general"));
    f.publisher.clear();
    ASSERT_TRUE(f.run(f.alice, "/join #general"));
    EXPECT_TRUE(f.events().empty());
}

TEST(Events, MessageCarriesAuthorTextAndNumber) {
    Fixture f;
    ASSERT_TRUE(f.run(f.alice, "/join #general"));
    f.publisher.clear();
    ASSERT_TRUE(f.run(f.alice, "/msg #general hello there"));
    ASSERT_TRUE(f.run(f.alice, "/msg #general :smile:"));
    ASSERT_EQ(f.events().size(), 2u);
    EXPECT_EQ(posted(f.events()[0]).author, "alice");
    EXPECT_EQ(posted(f.events()[0]).text, "hello there");
    EXPECT_EQ(posted(f.events()[0]).kind, "text");
    EXPECT_EQ(posted(f.events()[1]).kind, "reaction");
    EXPECT_LT(posted(f.events()[0]).seq, posted(f.events()[1]).seq);
}

TEST(Events, BroadcastIsOneMessageInEveryRoom) {
    Fixture f;
    ASSERT_TRUE(f.run(f.alice, "/join #a"));
    ASSERT_TRUE(f.run(f.alice, "/join #b"));
    f.publisher.clear();
    ASSERT_TRUE(f.run(f.alice, "/msg #a,#b hello both"));
    ASSERT_EQ(f.events().size(), 2u);
    EXPECT_EQ(posted(f.events()[0]).room, "#a");
    EXPECT_EQ(posted(f.events()[1]).room, "#b");
    EXPECT_EQ(posted(f.events()[0]).seq, posted(f.events()[1]).seq);   // одно сообщение
}

TEST(Events, FailedBroadcastPublishesNothing) {
    Fixture f;
    ASSERT_TRUE(f.run(f.alice, "/join #a"));
    f.publisher.clear();
    EXPECT_FALSE(f.run(f.alice, "/msg #a,#absent hi"));   // всё или ничего
    EXPECT_FALSE(f.run(f.bob, "/msg #a hi"));             // не участник
    EXPECT_TRUE(f.events().empty());
}

TEST(Events, LeavePublishesUserLeftThenNotice) {
    Fixture f;
    ASSERT_TRUE(f.run(f.alice, "/join #general"));
    f.publisher.clear();
    ASSERT_TRUE(f.run(f.alice, "/leave #general"));
    ASSERT_EQ(f.events().size(), 2u);
    EXPECT_EQ(std::get<UserLeft>(f.events()[0]).nick, "alice");
    EXPECT_EQ(posted(f.events()[1]).kind, "notice");
}

TEST(Events, KickIsLeaveAndNoticeInOrder) {
    Fixture f;
    ASSERT_TRUE(f.run(f.alice, "/join #general"));
    ASSERT_TRUE(f.run(f.bob, "/join #general"));
    f.publisher.clear();
    ASSERT_TRUE(f.run(f.alice, "/kick #general bob"));
    ASSERT_EQ(f.events().size(), 2u);
    EXPECT_EQ(std::get<UserLeft>(f.events()[0]).nick, "bob");
    EXPECT_EQ(posted(f.events()[1]).kind, "notice");
}

TEST(Events, LogoutLeavesEveryRoom) {
    Fixture f;
    ASSERT_TRUE(f.run(f.bob, "/join #a"));
    ASSERT_TRUE(f.run(f.bob, "/join #b"));
    ASSERT_TRUE(f.run(f.alice, "/join #c"));
    f.publisher.clear();
    f.server.logout(f.bob);
    ASSERT_EQ(f.events().size(), 2u);
    EXPECT_EQ(std::get<UserLeft>(f.events()[0]).room, "#a");
    EXPECT_EQ(std::get<UserLeft>(f.events()[1]).room, "#b");
}

TEST(Events, ReadsPublishNothing) {
    Fixture f;
    ASSERT_TRUE(f.run(f.alice, "/join #general"));
    ASSERT_TRUE(f.run(f.alice, "/msg #general hi"));
    f.publisher.clear();
    for (const char* command : {"/who #general", "/history #general", "/type #general 1", "/rooms", "/whois alice"}) {
        ASSERT_TRUE(f.run(f.alice, command)) << command;
    }
    EXPECT_TRUE(f.events().empty());
}

TEST(Events, PublisherFailureLosesEventButNotMessage) {
    Fixture f;
    ASSERT_TRUE(f.run(f.alice, "/join #general"));
    f.publisher.clear();
    f.publisher.fail(true);
    EXPECT_TRUE(f.run(f.alice, "/msg #general still here"));
    EXPECT_EQ(f.server.lost_events(), 1u);
    EXPECT_EQ(f.server.store().history("#general")->get().size(), 2u);   // уведомление и сообщение
}

TEST(Events, ServerWithoutPublisherRecordsNothing) {
    Server server;
    Session alice = server.login("alice");
    ASSERT_TRUE(server.handle(alice, "/join #general").ok);
    EXPECT_TRUE(server.store().take_events().empty());
}

TEST(Events, RoomOfEveryEvent) {
    EXPECT_EQ(chat::room_of(DomainEvent(MessagePosted{"#a", "x", "t", "text", 1})), "#a");
    EXPECT_EQ(chat::room_of(DomainEvent(UserJoined{"#b", "x"})), "#b");
    EXPECT_EQ(chat::room_of(DomainEvent(UserLeft{"#c", "x"})), "#c");
}
