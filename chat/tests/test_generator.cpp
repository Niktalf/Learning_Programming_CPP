// Звёздочка этапа 5: история генератором на std::generator.

#include <gtest/gtest.h>

#include <generator>
#include <string>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/message.hpp"

namespace {

int& guards_alive() {
    static int count = 0;
    return count;
}

// Локальный объект корутины, считающий себя.
struct Guard {
    Guard() { ++guards_alive(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    ~Guard() { --guards_alive(); }
};

std::generator<int> counting_with_guard(int limit) {
    const Guard guard;
    for (int i = 0; i < limit; ++i) {
        co_yield i;
    }
}

chat::ChatStore three_messages() {
    chat::ChatStore store;
    store.create_room("#general");
    for (const char* const text : {"first", "second", "third"}) {
        store.post("#general", chat::make_text("alice", text));
    }
    return store;
}

}  // namespace

TEST(Generator, YieldsNewestFirst) {
    const chat::ChatStore store = three_messages();
    std::vector<std::string> seen;
    for (const chat::MessagePtr& message : store.newest_first("#general")) {
        seen.push_back(message->to_string());
    }
    EXPECT_EQ(seen, (std::vector<std::string>{"third", "second", "first"}));
}

TEST(Generator, UnknownRoomYieldsNothing) {
    const chat::ChatStore store;
    int count = 0;
    for (const chat::MessagePtr& message : store.newest_first("#absent")) {
        static_cast<void>(message);
        ++count;
    }
    EXPECT_EQ(count, 0);
}

TEST(Generator, EarlyExitDestroysCoroutineLocals) {
    guards_alive() = 0;
    {
        auto numbers = counting_with_guard(1000);
        for (const int i : numbers) {
            EXPECT_EQ(guards_alive(), 1);   // корутина приостановлена, Guard жив
            if (i == 2) {
                break;
            }
        }
        EXPECT_EQ(guards_alive(), 1);
    }
    // Генератор разрушен — кадр корутины уничтожен в точке приостановки, и
    // Guard разрушен корректно: RAII работает и внутри корутины.
    EXPECT_EQ(guards_alive(), 0);
}
