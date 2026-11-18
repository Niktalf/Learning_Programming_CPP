// Этап 10: формат события в теле сообщения и защита потребителя от повторов
// и пропусков. Без брокера.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>

#include "kvs/event_wire.hpp"
#include "kvs/seen_events.hpp"

using kvs::DomainEvent;
using kvs::EventEnvelope;
using kvs::KeyChanged;
using kvs::KeyDeleted;
using kvs::KeyExpired;

TEST(EventWire, ChangedIsReadable) {
    const std::string payload = kvs::encode_event({"i:7", KeyChanged{"user:42", "string", 3}});
    EXPECT_EQ(payload, "i:7\tchanged\tuser:42\tstring\t3");
}

TEST(EventWire, RoundTripEveryKind) {
    for (const DomainEvent& event :
         {DomainEvent(KeyChanged{"a", "list", 12}), DomainEvent(KeyDeleted{"b"}), DomainEvent(KeyExpired{"c"})}) {
        const std::optional<EventEnvelope> decoded = kvs::decode_event(kvs::encode_event({"x:1", event}));
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->id, "x:1");
        EXPECT_EQ(decoded->event.index(), event.index());
        EXPECT_EQ(kvs::key_of(decoded->event), kvs::key_of(event));
    }
}

TEST(EventWire, KeyWithSeparatorsSurvives) {
    const std::string key = "a\tb\nc\\d";
    const auto decoded = kvs::decode_event(kvs::encode_event({"x:1", KeyDeleted{key}}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(kvs::key_of(decoded->event), key);
}

TEST(EventWire, ForeignPayloadsAreRejected) {
    for (const char* payload : {"", "hello", "x:1\tchanged\ta", "x:1\tchanged\ta\tstring\tmany",
                                "x:1\tmoved\ta", "\tdeleted\ta", "x:1\tdeleted\ta\\", "x:1\tdeleted\ta\textra"}) {
        EXPECT_FALSE(kvs::decode_event(payload).has_value()) << payload;
    }
}

TEST(EventWire, DescribeForHumans) {
    EXPECT_EQ(kvs::describe_event(KeyChanged{"a", "int", 2}), "changed a (int, generation 2)");
    EXPECT_EQ(kvs::describe_event(KeyDeleted{"a"}), "deleted a");
}

TEST(SeenEvents, RepeatIsRecognised) {
    kvs::SeenEvents seen(10);
    EXPECT_TRUE(seen.first_time("p:1"));
    EXPECT_TRUE(seen.first_time("p:2"));
    EXPECT_FALSE(seen.first_time("p:1"));
    EXPECT_EQ(seen.size(), 2u);
}

TEST(SeenEvents, MemoryIsBounded) {
    kvs::SeenEvents seen(2);
    EXPECT_TRUE(seen.first_time("p:1"));
    EXPECT_TRUE(seen.first_time("p:2"));
    EXPECT_TRUE(seen.first_time("p:3"));   // вытесняет p:1
    EXPECT_EQ(seen.size(), 2u);
    EXPECT_FALSE(seen.first_time("p:3"));
    EXPECT_TRUE(seen.first_time("p:1"));   // забыт — цена границы
}

TEST(GenerationTracker, ConsecutiveGenerationsHaveNoGap) {
    kvs::GenerationTracker tracker;
    EXPECT_FALSE(tracker.observe(KeyChanged{"a", "string", 5}));   // подключились посреди потока
    EXPECT_FALSE(tracker.observe(KeyChanged{"a", "string", 6}));
    EXPECT_FALSE(tracker.observe(KeyDeleted{"a"}));
    EXPECT_FALSE(tracker.observe(KeyChanged{"a", "string", 1}));
}

TEST(GenerationTracker, LostEventIsNoticed) {
    kvs::GenerationTracker tracker;
    EXPECT_FALSE(tracker.observe(KeyChanged{"a", "string", 1}));
    const auto gap = tracker.observe(KeyChanged{"a", "string", 3});
    ASSERT_TRUE(gap.has_value());
    EXPECT_EQ(gap->expected, 2u);
    EXPECT_EQ(gap->got, 3u);
}

TEST(GenerationTracker, LostDeleteIsNoticed) {
    // Удаление потеряно: после поколения 4 сразу 1.
    kvs::GenerationTracker tracker;
    EXPECT_FALSE(tracker.observe(KeyChanged{"a", "string", 4}));
    EXPECT_TRUE(tracker.observe(KeyChanged{"a", "string", 1}).has_value());
}
