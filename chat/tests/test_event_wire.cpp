// Этап 10: формат события в теле сообщения и защита потребителя от повторов
// и пропусков. Без брокера.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>

#include "chat/event_wire.hpp"
#include "chat/seen_events.hpp"

using chat::DomainEvent;
using chat::EventEnvelope;
using chat::MessagePosted;
using chat::UserJoined;
using chat::UserLeft;

TEST(EventWire, PostedIsReadable) {
    const std::string payload = chat::encode_event({"p", 3, MessagePosted{"#general", "alice", "hi", "text", 17}});
    EXPECT_EQ(payload, "p\t3\tposted\t#general\talice\ttext\t17\thi");
}

TEST(EventWire, RoundTripEveryKind) {
    for (const DomainEvent& event : {DomainEvent(MessagePosted{"#a", "alice", "two\twords", "text", 5}),
                                     DomainEvent(UserJoined{"#b", "bob"}), DomainEvent(UserLeft{"#c", "carol"})}) {
        const std::optional<EventEnvelope> decoded = chat::decode_event(chat::encode_event({"p", 9, event}));
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->publisher, "p");
        EXPECT_EQ(decoded->number, 9u);
        EXPECT_EQ(decoded->event.index(), event.index());
        EXPECT_EQ(chat::room_of(decoded->event), chat::room_of(event));
    }
}

TEST(EventWire, TextWithSeparatorsSurvives) {
    const std::string text = "a\tb\nc\\d";
    const auto decoded = chat::decode_event(chat::encode_event({"p", 1, MessagePosted{"#a", "x", text, "text", 1}}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(std::get<MessagePosted>(decoded->event).text, text);
}

TEST(EventWire, ForeignPayloadsAreRejected) {
    for (const char* payload : {"", "hello", "p\t1\tjoined\t#a", "p\tx\tjoined\t#a\tbob", "p\t1\twaved\t#a\tbob",
                                "\t1\tjoined\t#a\tbob", "p\t1\tposted\t#a\tbob\ttext\tmany\thi",
                                "p\t1\tleft\t#a\tbob\textra"}) {
        EXPECT_FALSE(chat::decode_event(payload).has_value()) << payload;
    }
}

TEST(EventWire, IdNamesPublisherRoomAndNumber) {
    const EventEnvelope envelope{"p", 4, UserJoined{"#a", "bob"}};
    EXPECT_EQ(envelope.id(), "p:#a:4");
}

TEST(EventWire, DescribeForHumans) {
    EXPECT_EQ(chat::describe_event(MessagePosted{"#a", "alice", "hi", "text", 3}), "#a <alice> hi (#3, text)");
    EXPECT_EQ(chat::describe_event(UserJoined{"#a", "bob"}), "#a + bob joined");
    EXPECT_EQ(chat::describe_event(UserLeft{"#a", "bob"}), "#a - bob left");
}

TEST(SeenEvents, RepeatIsRecognisedAndMemoryIsBounded) {
    chat::SeenEvents seen(2);
    EXPECT_TRUE(seen.first_time("p:#a:1"));
    EXPECT_FALSE(seen.first_time("p:#a:1"));
    EXPECT_TRUE(seen.first_time("p:#a:2"));
    EXPECT_TRUE(seen.first_time("p:#a:3"));   // вытесняет первое
    EXPECT_EQ(seen.size(), 2u);
    EXPECT_TRUE(seen.first_time("p:#a:1"));   // забыто — цена границы
}

TEST(SequenceTracker, ConsecutiveNumbersHaveNoGap) {
    chat::SequenceTracker tracker;
    EXPECT_FALSE(tracker.observe("p", "#a", 7));   // подключились посреди потока
    EXPECT_FALSE(tracker.observe("p", "#a", 8));
    EXPECT_FALSE(tracker.observe("p", "#b", 1));   // своя нумерация у каждой комнаты
    EXPECT_FALSE(tracker.observe("q", "#a", 1));   // сервер перезапущен
}

TEST(SequenceTracker, LostEventIsNoticed) {
    chat::SequenceTracker tracker;
    EXPECT_FALSE(tracker.observe("p", "#a", 1));
    const auto gap = tracker.observe("p", "#a", 4);
    ASSERT_TRUE(gap.has_value());
    EXPECT_EQ(gap->expected, 2u);
    EXPECT_EQ(gap->got, 4u);
}
