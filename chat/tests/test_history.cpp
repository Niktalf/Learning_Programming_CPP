// Этап 5: история через ranges. Главное — доказать ленивость счётчиком.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/message.hpp"

namespace {

// Комната из тысячи сообщений: чередуются alice и bob.
chat::ChatStore thousand_messages() {
    chat::ChatStore store;
    store.create_room("#general");
    for (int i = 0; i < 1000; ++i) {
        store.post("#general", chat::make_text(i % 2 == 0 ? "alice" : "bob", "m" + std::to_string(i)));
    }
    return store;
}

std::vector<std::size_t> indices(const std::vector<chat::ChatStore::Entry>& entries) {
    std::vector<std::size_t> result;
    for (const auto& entry : entries) {
        result.push_back(entry.index);
    }
    return result;
}

}  // namespace

TEST(History, PipelineIsLazy) {
    const chat::ChatStore store = thousand_messages();
    int calls = 0;
    const auto selected = store.history_where(
        "#general",
        [&calls](const chat::MessagePtr&) {
            ++calls;
            return true;
        },
        10);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->size(), 10u);
    // Предикат вызван одиннадцать раз, а не тысячу: take остановил обход.
    // Одиннадцатый вызов — продвижение filter после последнего элемента:
    // filter ищет следующий подходящий, и только потом take видит, что
    // предел набран. Материализация до take дала бы 1000.
    EXPECT_EQ(calls, 11);
}

TEST(History, NewestMatchesInChronologicalOrder) {
    const chat::ChatStore store = thousand_messages();
    const auto bob = store.history_where(
        "#general", [](const chat::MessagePtr& m) { return m->author() == "bob"; }, 3);
    // Последние три сообщения bob — нечётные номера с конца, по возрастанию.
    EXPECT_EQ(indices(*bob), (std::vector<std::size_t>{995, 997, 999}));
    EXPECT_EQ(bob->back().message.get()->to_string(), std::string("m999"));
}

TEST(History, FilterBeforeTakeIsNotTakeBeforeFilter) {
    const chat::ChatStore store = thousand_messages();
    const auto alice = store.history_where(
        "#general", [](const chat::MessagePtr& m) { return m->author() == "alice"; }, 2);
    // filter | take: два последних сообщения alice. take | filter взял бы два
    // последних сообщения подряд — 998 и 999 — и оставил бы одно.
    EXPECT_EQ(indices(*alice), (std::vector<std::size_t>{996, 998}));
}

TEST(History, UnknownRoomIsEmptyOptional) {
    const chat::ChatStore store;
    const auto selected = store.history_where("#absent", [](const chat::MessagePtr&) { return true; }, 5);
    EXPECT_FALSE(selected.has_value());
}

TEST(History, HugeLimitIsClampedNotNegative) {
    // Находка фаззера: size_t больше PTRDIFF_MAX, переданный в views::take,
    // становился отрицательной разностью итераторов.
    const chat::ChatStore store = thousand_messages();
    const auto all = store.history_where(
        "#general", [](const chat::MessagePtr&) { return true; }, static_cast<std::size_t>(-1));
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->size(), 1000u);
}
