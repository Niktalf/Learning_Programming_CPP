// Этап 8: индекс истории по времени — /since и /until.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/commands.hpp"
#include "chat/message.hpp"

using chat::ChatStore;
using chat::Session;

namespace {

// Часы, которые идут только когда им скажут.
struct ManualClock {
    std::uint64_t now = 1000;
    std::uint64_t operator()() const { return now; }
};

std::string run(ChatStore& store, Session& session, const std::string& text) {
    return chat::execute(store, session, text).text;
}

// Хранилище с участником alice в #general и управляемыми часами.
struct Fixture {
    explicit Fixture(std::size_t capacity = ChatStore::default_history_capacity) : store(capacity) {
        store.set_clock([this] { return clock.now; });
        store.register_user("alice");
        run(store, alice, "/join #general");   // уведомление о входе — в момент 1000
    }

    void say_at(std::uint64_t moment, const std::string& text) {
        clock.now = moment;
        run(store, alice, "/msg #general " + text);
    }

    std::vector<std::size_t> indexes(std::uint64_t since, std::uint64_t until, std::size_t limit,
                                     ChatStore::Window window) const {
        // Результат — в переменную. В for (x : *f()) временный optional
        // живёт до конца цикла только по правилу P2718 из C++23; GCC 15 его
        // реализует, Clang 20 — нет, и там обход шёл бы по разрушенному
        // объекту. Clang об этом и предупреждает.
        const auto selected = store.history_between("#general", since, until, limit, window);
        std::vector<std::size_t> result;
        for (const auto& entry : *selected) {
            result.push_back(entry.index);
        }
        return result;
    }

    ManualClock clock;
    ChatStore store;
    Session alice{"alice"};
};

constexpr std::uint64_t forever = std::numeric_limits<std::uint64_t>::max();

}  // namespace

TEST(TimeIndex, WindowIsInclusiveOnBothEnds) {
    Fixture chat;
    chat.say_at(2000, "a");   // номер 1
    chat.say_at(3000, "b");   // 2
    chat.say_at(4000, "c");   // 3
    EXPECT_EQ(chat.indexes(2000, 3000, 10, ChatStore::Window::Earliest), (std::vector<std::size_t>{1, 2}));
    EXPECT_EQ(chat.indexes(2001, 3999, 10, ChatStore::Window::Earliest), (std::vector<std::size_t>{2}));
    EXPECT_TRUE(chat.indexes(5000, forever, 10, ChatStore::Window::Earliest).empty());
    EXPECT_TRUE(chat.indexes(3000, 2000, 10, ChatStore::Window::Earliest).empty());   // пустое окно
}

TEST(TimeIndex, EarliestAndLatestTakeFromOppositeEnds) {
    Fixture chat;
    for (std::uint64_t t = 1; t <= 6; ++t) {
        chat.say_at(1000 + t * 100, "m" + std::to_string(t));   // номера 1..6
    }
    EXPECT_EQ(chat.indexes(0, forever, 2, ChatStore::Window::Earliest), (std::vector<std::size_t>{0, 1}));
    // Последние — тоже в хронологическом порядке.
    EXPECT_EQ(chat.indexes(0, forever, 2, ChatStore::Window::Latest), (std::vector<std::size_t>{5, 6}));
}

TEST(TimeIndex, SameMillisecondKeepsSendingOrder) {
    // Ключ индекса — пара «время, номер»: в одну миллисекунду два сообщения,
    // и оба на месте, в порядке отправки.
    Fixture chat;
    chat.say_at(5000, "first");
    chat.say_at(5000, "second");
    EXPECT_EQ(chat.indexes(5000, 5000, 10, ChatStore::Window::Earliest), (std::vector<std::size_t>{1, 2}));
}

TEST(TimeIndex, EvictedMessagesLeaveTheIndex) {
    // История на три сообщения: вытесненное уходит и из индекса — иначе
    // поиск по времени вернул бы номер, которого в истории больше нет.
    Fixture chat(3);
    for (std::uint64_t t = 1; t <= 5; ++t) {
        chat.say_at(1000 + t, "m" + std::to_string(t));   // номера 1..5, в истории 3..5
    }
    EXPECT_EQ(chat.indexes(0, forever, 100, ChatStore::Window::Earliest), (std::vector<std::size_t>{3, 4, 5}));
}

TEST(TimeIndex, RecordedByEveryWayOfPosting) {
    // post, рассылка в несколько комнат и /kick — все пишут в индекс.
    Fixture chat;
    chat.store.register_user("bob");
    Session bob{"bob"};
    run(chat.store, bob, "/join #general");          // 1
    run(chat.store, chat.alice, "/join #other");     // в #other: 0
    chat.clock.now = 7000;
    run(chat.store, chat.alice, "/msg #general,#other hi");   // #general 2, #other 1
    run(chat.store, chat.alice, "/kick #general bob");        // 3
    EXPECT_EQ(chat.indexes(7000, 7000, 10, ChatStore::Window::Earliest), (std::vector<std::size_t>{2, 3}));
    EXPECT_EQ(chat.store.history_between("#other", 7000, 7000, 10, ChatStore::Window::Earliest)->size(), 1u);
}

TEST(TimeIndex, Commands) {
    Fixture chat;
    chat.say_at(2000, "hello");
    chat.say_at(3000, "world");
    EXPECT_EQ(run(chat.store, chat.alice, "/since #general 2000"),
              std::string("1 @2000: <alice> hello\n2 @3000: <alice> world"));
    EXPECT_EQ(run(chat.store, chat.alice, "/since #general 2000 1"), std::string("1 @2000: <alice> hello"));
    EXPECT_EQ(run(chat.store, chat.alice, "/until #general 2999"),
              std::string("0 @1000: <alice> * alice joined\n1 @2000: <alice> hello"));
    EXPECT_EQ(run(chat.store, chat.alice, "/until #general 999"), std::string("(empty)"));
}

TEST(TimeIndex, HugeLimitIsSafe) {
    // Тот же урок фаззера, что у /history: предел обрезается до take.
    Fixture chat;
    EXPECT_EQ(run(chat.store, chat.alice, "/since #general 0 18446744073709551615"),
              std::string("0 @1000: <alice> * alice joined"));
}
