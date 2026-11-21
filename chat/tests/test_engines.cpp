// Этап 8: одни и те же проверки на каждом движке хранилища. Остальные тесты
// гоняются на всех движках через CHAT_ENGINE (CMakeLists.txt); эти — явно
// параметризованы, чтобы сравнение было в одном месте.

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/engines.hpp"
#include "chat/errors.hpp"
#include "chat/message.hpp"

using chat::ChatStore;
using chat::EngineKind;

class Engines : public ::testing::TestWithParam<EngineKind> {};

INSTANTIATE_TEST_SUITE_P(All, Engines, ::testing::ValuesIn(chat::all_engine_kinds),
                         [](const ::testing::TestParamInfo<EngineKind>& param) {
                             std::string name(chat::name_of(param.param));
                             std::erase(name, '-');   // имя теста gtest — только буквы и цифры
                             return name;
                         });

TEST_P(Engines, ReportsItsName) {
    const ChatStore store(GetParam());
    EXPECT_EQ(store.engine_name(), chat::name_of(GetParam()));
}

TEST_P(Engines, ManyRoomsSurviveGrowth) {
    // Тысяча комнат — таблица комнат несколько раз растёт. Каждая находится,
    // у каждой своя история.
    ChatStore store(GetParam());
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(store.create_room("#room" + std::to_string(i)));
    }
    EXPECT_FALSE(store.create_room("#room7"));
    EXPECT_EQ(store.room_count(), 1000u);
    for (int i = 0; i < 1000; i += 37) {
        const std::string room = "#room" + std::to_string(i);
        store.post(room, chat::make_text("bot", room));
    }
    for (int i = 0; i < 1000; i += 37) {
        const std::string room = "#room" + std::to_string(i);
        ASSERT_TRUE(store.contains(room));
        EXPECT_EQ(store.history(room)->get().back()->to_string(), room);
    }
}

TEST_P(Engines, RoomsAreListedAlphabeticallyOnEveryEngine) {
    // Порядок держит упорядоченный индекс имён, а не таблица движка.
    ChatStore store(GetParam());
    for (const char* const room : {"#zeta", "#alpha", "#mid", "#beta"}) {
        store.create_room(room);
    }
    std::vector<std::string> seen;
    store.for_each_room([&seen](const std::string& room, const chat::History&) { seen.push_back(room); });
    EXPECT_EQ(seen, (std::vector<std::string>{"#alpha", "#beta", "#mid", "#zeta"}));
}

TEST_P(Engines, NickRegistryChurn) {
    // Входят и выходят: на открытой адресации это и есть надгробия.
    ChatStore store(GetParam());
    for (int i = 0; i < 200; ++i) {
        store.register_user("user" + std::to_string(i));
    }
    for (int i = 0; i < 5000; ++i) {
        store.release_user("user" + std::to_string(i));
        store.register_user("user" + std::to_string(i + 200));
    }
    EXPECT_EQ(store.user_count(), 200u);
    EXPECT_FALSE(store.has_user("user4999"));
    EXPECT_TRUE(store.has_user("user5000"));
    EXPECT_TRUE(store.has_user("user5199"));
}

TEST_P(Engines, RenameMovesTheRecord) {
    ChatStore store(GetParam());
    const auto alice = store.register_user("alice");
    store.register_user("bob");
    store.join("#general", "alice");
    store.rename("alice", "alicia");
    EXPECT_FALSE(store.has_user("alice"));
    EXPECT_EQ(store.find_user("alicia"), alice);
    EXPECT_EQ(alice->nick(), "alicia");
    EXPECT_TRUE(store.is_member("#general", "alicia"));
    EXPECT_THROW(store.rename("alicia", "bob"), chat::NicknameTakenError);
    EXPECT_THROW(store.rename("nobody", "carol"), chat::UnknownUserError);
    EXPECT_EQ(store.user_count(), 2u);
}

TEST_P(Engines, DepartedMembersArePurgedEverywhere) {
    ChatStore store(GetParam());
    store.register_user("alice");
    for (int i = 0; i < 50; ++i) {
        store.join("#r" + std::to_string(i), "alice");
    }
    store.release_user("alice");
    EXPECT_EQ(store.purge_departed(), 50u);
}
