// Этап 10: доменные события — какие команды что публикуют. Без брокера:
// издатель подставной (FakePublisher).

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

#include "fake_publisher.hpp"
#include "kvs/events.hpp"
#include "kvs/server.hpp"
#include "kvs/store.hpp"
#include "memory_log.hpp"

using kvs::DomainEvent;
using kvs::KeyChanged;
using kvs::KeyDeleted;
using kvs::Server;
using kvs::testing::FakePublisher;
using kvs::testing::MemoryLog;

namespace {

// Сервер с подставным издателем; тест держит свою копию издателя.
struct Fixture {
    FakePublisher publisher;
    Server server{kvs::Store(), nullptr, nullptr, publisher.port()};

    [[nodiscard]] const std::vector<DomainEvent>& events() const { return publisher.events(); }
};

[[nodiscard]] const KeyChanged& changed(const DomainEvent& event) { return std::get<KeyChanged>(event); }

}  // namespace

TEST(Events, SetPublishesKeyChangedWithGeneration) {
    Fixture f;
    ASSERT_TRUE(f.server.handle("SET a 1").ok);
    ASSERT_TRUE(f.server.handle("SET a hello").ok);
    ASSERT_EQ(f.events().size(), 2u);
    EXPECT_EQ(changed(f.events()[0]).key, "a");
    EXPECT_EQ(changed(f.events()[0]).generation, 1u);
    EXPECT_EQ(changed(f.events()[1]).generation, 2u);
    EXPECT_EQ(changed(f.events()[1]).type, "string");
}

TEST(Events, TypeFollowsValue) {
    Fixture f;
    ASSERT_TRUE(f.server.handle("INCR n").ok);
    ASSERT_TRUE(f.server.handle("RPUSH l x").ok);
    ASSERT_EQ(f.events().size(), 2u);
    EXPECT_EQ(changed(f.events()[0]).type, "int");
    EXPECT_EQ(changed(f.events()[1]).type, "list");
}

TEST(Events, InPlaceChangesArePublished) {
    Fixture f;
    ASSERT_TRUE(f.server.handle("RPUSH l a").ok);
    ASSERT_TRUE(f.server.handle("RPUSH l b").ok);
    ASSERT_TRUE(f.server.handle("LPOP l").ok);
    ASSERT_TRUE(f.server.handle("SET s x").ok);
    ASSERT_TRUE(f.server.handle("APPEND s y").ok);
    ASSERT_EQ(f.events().size(), 5u);
    EXPECT_EQ(changed(f.events()[2]).generation, 3u);   // LPOP — третья запись в l
    EXPECT_EQ(changed(f.events()[4]).key, "s");
    EXPECT_EQ(changed(f.events()[4]).generation, 2u);
}

TEST(Events, DelPublishesOnlyWhenKeyExisted) {
    Fixture f;
    ASSERT_TRUE(f.server.handle("SET a 1").ok);
    ASSERT_TRUE(f.server.handle("DEL a").ok);
    EXPECT_FALSE(f.server.handle("DEL a").ok);   // ключа уже нет — ничего не произошло
    ASSERT_EQ(f.events().size(), 2u);
    EXPECT_EQ(std::get<KeyDeleted>(f.events()[1]).key, "a");
}

TEST(Events, ReadsPublishNothing) {
    Fixture f;
    ASSERT_TRUE(f.server.handle("SET a 1").ok);
    f.publisher.clear();
    for (const char* command : {"GET a", "EXISTS a", "TYPE a", "KEYS *", "VERSION a", "STRLEN a", "MEMORY"}) {
        ASSERT_TRUE(f.server.handle(command).ok) << command;
    }
    EXPECT_TRUE(f.events().empty());
}

TEST(Events, FailedCommandsPublishNothing) {
    Fixture f;
    ASSERT_TRUE(f.server.handle("SET s text").ok);
    f.publisher.clear();
    EXPECT_FALSE(f.server.handle("INCR s").ok);       // не число
    EXPECT_FALSE(f.server.handle("RPUSH s x").ok);    // не список
    EXPECT_FALSE(f.server.handle("SET").ok);          // разбор
    EXPECT_FALSE(f.server.handle("LPOP s").ok);       // не список
    EXPECT_TRUE(f.events().empty());
}

TEST(Events, MsetPublishesEveryPairInOrder) {
    Fixture f;
    ASSERT_TRUE(f.server.handle("MSET a 1 b 2 a 3").ok);
    ASSERT_EQ(f.events().size(), 3u);
    EXPECT_EQ(changed(f.events()[0]).key, "a");
    EXPECT_EQ(changed(f.events()[1]).key, "b");
    EXPECT_EQ(changed(f.events()[2]).generation, 2u);
}

TEST(Events, FailedMsetPublishesNothing) {
    // Предел объёма отвергает вторую пару — вся команда не выполнена, и
    // событие первой пары, построенное на копии, уходит вместе с копией.
    FakePublisher publisher;
    Server server(kvs::Store(std::size_t{8}), nullptr, nullptr, publisher.port());
    EXPECT_FALSE(server.handle("MSET a 1 b 0123456789").ok);
    EXPECT_TRUE(publisher.events().empty());
    EXPECT_FALSE(server.store().contains("a"));
}

TEST(Events, ReplayedHistoryIsNotPublished) {
    // Эти записи были опубликованы в прошлой жизни сервера.
    const MemoryLog history({"SET a 1", "SET b 2", "DEL a"});
    FakePublisher publisher;
    Server server(kvs::Store(), history.sink(), &history, publisher.port());
    EXPECT_TRUE(publisher.events().empty());
    ASSERT_TRUE(server.handle("SET b 3").ok);
    ASSERT_EQ(publisher.events().size(), 1u);
    EXPECT_EQ(changed(publisher.events()[0]).generation, 2u);   // поколение — с учётом истории
}

TEST(Events, PublisherFailureLosesEventButNotCommand) {
    Fixture f;
    f.publisher.fail(true);
    const auto response = f.server.handle("SET a 1");
    EXPECT_TRUE(response.ok);
    EXPECT_EQ(f.server.lost_events(), 1u);
    EXPECT_TRUE(f.server.store().contains("a"));

    // Брокер вернулся — следующие события уходят, потерянное не всплывает.
    f.publisher.fail(false);
    ASSERT_TRUE(f.server.handle("SET a 2").ok);
    ASSERT_EQ(f.events().size(), 1u);
    EXPECT_EQ(changed(f.events()[0]).generation, 2u);   // пропуск виден по поколению
}

TEST(Events, ServerWithoutPublisherRecordsNothing) {
    Server server;
    ASSERT_TRUE(server.handle("SET a 1").ok);
    kvs::Store copy = server.store();
    EXPECT_TRUE(copy.take_events().empty());
}

TEST(Events, StoreRecordsOnlyWhenAsked) {
    kvs::Store store;
    store.set("a", kvs::Value(std::string("1")));
    EXPECT_TRUE(store.take_events().empty());
    store.record_events(true);
    store.set("a", kvs::Value(std::string("2")));
    const std::vector<DomainEvent> taken = store.take_events();
    ASSERT_EQ(taken.size(), 1u);
    EXPECT_EQ(changed(taken[0]).generation, 2u);
    EXPECT_TRUE(store.take_events().empty());
}

TEST(Events, KeyOfEveryEvent) {
    EXPECT_EQ(kvs::key_of(DomainEvent(KeyChanged{"a", "string", 1})), "a");
    EXPECT_EQ(kvs::key_of(DomainEvent(KeyDeleted{"b"})), "b");
    EXPECT_EQ(kvs::key_of(DomainEvent(kvs::KeyExpired{"c"})), "c");
}
