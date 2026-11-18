// Этап 9: часы — зависимость хранилища. Двое часов: системные — для отметки
// сообщения, монотонные — для простоя. Тесты сдвигают подставные часы и не
// ждут ни секунды реального времени.

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "chat/chat_store.hpp"
#include "chat/commands.hpp"
#include "chat/server.hpp"
#include "manual_clock.hpp"

using chat::ChatStore;
using chat::Session;
using chat::testing::ManualClock;
using std::chrono::seconds;

namespace {

ChatStore store_with(const ManualClock& clock) {
    return ChatStore(chat::make_chat_store<ChatStore::Room>(chat::default_engine_kind()),
                     ChatStore::default_history_capacity, clock);
}

}  // namespace

TEST(Clock, MessageTimestampIsWallTime) {
    ManualClock clock;
    clock.set_wall_ms(1792940637000);
    ChatStore store = store_with(clock);
    EXPECT_EQ(store.next_message_id("alice").timestamp, 1792940637000u);
}

TEST(Clock, WhoisReportsIdleTimeInstantly) {
    ManualClock clock;
    chat::Server server(store_with(clock));
    Session alice = server.login("alice");
    Session bob = server.login("bob");
    static_cast<void>(server.handle(alice, "/join #general"));
    clock.advance(seconds(600));   // десять минут — мгновенно
    static_cast<void>(server.handle(bob, "/join #general"));
    EXPECT_EQ(server.handle(bob, "/whois alice").text, std::string("alice, idle 600s"));
    // Команда самого bob — его активность: простой bob — ноль.
    EXPECT_EQ(server.handle(alice, "/whois bob").text, std::string("bob, idle 0s"));
    EXPECT_EQ(server.handle(alice, "/whois nobody").text,
              std::string("ERR no such user: nobody (while executing /whois nobody)"));
}

TEST(Clock, IdleSurvivesRename) {
    // Простой — свойство пользователя, а не ника.
    ManualClock clock;
    chat::Server server(store_with(clock));
    Session alice = server.login("alice");
    clock.advance(seconds(5));
    static_cast<void>(server.handle(alice, "/nick alicia"));
    clock.advance(seconds(3));
    EXPECT_EQ(server.store().idle_time("alicia"), std::chrono::steady_clock::duration(seconds(3)));
}
