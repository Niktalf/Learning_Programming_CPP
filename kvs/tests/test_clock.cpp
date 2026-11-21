// Этап 9: часы — зависимость хранилища. Тесты сдвигают подставные часы и не
// ждут ни одной секунды реального времени.

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "kvs/commands.hpp"
#include "kvs/store.hpp"
#include "manual_clock.hpp"

using std::chrono::seconds;
using kvs::testing::ManualClock;

namespace {

kvs::Store store_with(const ManualClock& clock) {
    return kvs::Store(kvs::make_engine<kvs::StoreEntry<kvs::Value>>(kvs::default_engine_kind()), std::nullopt,
                      clock);
}

}  // namespace

TEST(Clock, IdleTimeFollowsTheInjectedClock) {
    ManualClock clock;
    kvs::Store store = store_with(clock);
    store.set("k", kvs::Value(std::string("v")));
    clock.advance(seconds(600));   // десять минут — мгновенно
    EXPECT_EQ(store.idle_time("k"), std::chrono::steady_clock::duration(seconds(600)));
    EXPECT_FALSE(store.idle_time("absent"));
}

TEST(Clock, EveryWriteResetsIdleTime) {
    ManualClock clock;
    kvs::Store store = store_with(clock);
    store.set("list", kvs::Value(std::string("x")));
    clock.advance(seconds(30));
    static_cast<void>(kvs::execute(store, "SET list again"));
    EXPECT_EQ(kvs::execute(store, "IDLETIME list").text, std::string("0"));
    static_cast<void>(kvs::execute(store, "DEL list"));
    static_cast<void>(kvs::execute(store, "RPUSH l a"));
    clock.advance(seconds(5));
    static_cast<void>(kvs::execute(store, "RPUSH l b"));   // изменение на месте — тоже запись
    clock.advance(seconds(2));
    EXPECT_EQ(kvs::execute(store, "IDLETIME l").text, std::string("2"));
}

TEST(Clock, ReadsDoNotTouch) {
    ManualClock clock;
    kvs::Store store = store_with(clock);
    static_cast<void>(kvs::execute(store, "SET k v"));
    clock.advance(seconds(7));
    static_cast<void>(kvs::execute(store, "GET k"));
    EXPECT_EQ(kvs::execute(store, "IDLETIME k").text, std::string("7"));
}

TEST(Clock, CopyKeepsTheClock) {
    // MSET работает над копией хранилища: копия обязана смотреть на те же
    // часы, иначе после MSET время записей шло бы от других часов.
    ManualClock clock;
    kvs::Store store = store_with(clock);
    clock.advance(seconds(100));
    store.mset({{"a", kvs::Value(std::string("1"))}});
    clock.advance(seconds(3));
    EXPECT_EQ(store.idle_time("a"), std::chrono::steady_clock::duration(seconds(3)));
}
