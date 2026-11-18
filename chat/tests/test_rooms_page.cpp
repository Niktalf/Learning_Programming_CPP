// Этап 8: /rooms с курсором.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/commands.hpp"

using chat::ChatStore;
using chat::Session;

namespace {

ChatStore numbered(int count) {
    ChatStore store;
    for (int i = 0; i < count; ++i) {
        store.create_room("#r" + std::to_string(1000 + i));
    }
    return store;
}

}  // namespace

TEST(RoomsPage, PagesCoverEveryRoomOnceInOrder) {
    const ChatStore store = numbered(120);
    std::vector<std::string> seen;
    std::optional<std::string> cursor;
    int pages = 0;
    for (;;) {
        const auto page = store.room_page(cursor, 50);
        seen.insert(seen.end(), page.names.begin(), page.names.end());
        ++pages;
        if (!page.more) {
            break;
        }
        cursor = page.names.back();
    }
    EXPECT_EQ(pages, 3);
    ASSERT_EQ(seen.size(), 120u);
    EXPECT_TRUE(std::ranges::is_sorted(seen));
    EXPECT_EQ(std::set<std::string>(seen.begin(), seen.end()).size(), 120u);
}

TEST(RoomsPage, NewRoomsDoNotShiftTheCursor) {
    // Курсор — имя, а не номер: комнаты, созданные между вызовами, не
    // заставляют существующие пропасть или повториться.
    ChatStore store = numbered(10);
    const auto first = store.room_page(std::nullopt, 5);
    ASSERT_TRUE(first.more);
    store.create_room("#a");     // до курсора — в эту страницу уже не попадёт
    store.create_room("#r1007x");   // после курсора — попадёт
    const auto second = store.room_page(first.names.back(), 100);
    EXPECT_EQ(second.names.front(), "#r1005");
    EXPECT_EQ(second.names.size(), 6u);
    EXPECT_FALSE(second.more);
}

TEST(RoomsPage, ExactFitHasNoMore) {
    const ChatStore store = numbered(4);
    EXPECT_FALSE(store.room_page(std::nullopt, 4).more);
    EXPECT_TRUE(store.room_page(std::nullopt, 3).more);
    EXPECT_TRUE(store.room_page(std::string("#zzz"), 10).names.empty());
}

TEST(RoomsPage, CommandPrintsHintForTheNextPage) {
    ChatStore store = numbered(3);
    store.register_user("alice");
    Session alice{"alice"};
    static_cast<void>(chat::execute(store, alice, "/join #r1001"));
    EXPECT_EQ(chat::execute(store, alice, "/rooms #r1000 1").text,
              std::string("#r1001 (1)\n(more: /rooms #r1001 1)"));
    EXPECT_EQ(chat::execute(store, alice, "/rooms #r1001 5").text, std::string("#r1002 (0)"));
}
