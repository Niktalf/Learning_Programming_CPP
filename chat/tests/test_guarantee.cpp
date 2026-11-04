// Сильная гарантия рассылки в несколько комнат. Рассуждения на словах
// доказательством не являются — нужен тип, который бросает в заданный момент.

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/errors.hpp"
#include "chat/message.hpp"

namespace {

// Сколько копирований осталось до броска; -1 — не бросать.
int& countdown() {
    static int n = -1;
    return n;
}

class ThrowOnNthCopy {
public:
    ThrowOnNthCopy() = default;
    explicit ThrowOnNthCopy(std::string text) : text_(std::move(text)) {}

    ThrowOnNthCopy(const ThrowOnNthCopy& other) : text_(other.text_) { tick(); }
    ThrowOnNthCopy& operator=(const ThrowOnNthCopy& other) {
        tick();
        text_ = other.text_;
        return *this;
    }
    // Перемещение не бросает — ровно как у настоящего Message. Без этого
    // post_many не собрался бы: static_assert внутри требует noexcept.
    ThrowOnNthCopy(ThrowOnNthCopy&&) noexcept = default;
    ThrowOnNthCopy& operator=(ThrowOnNthCopy&&) noexcept = default;

    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    static void tick() {
        if (countdown() > 0 && --countdown() == 0) {
            throw std::bad_alloc();
        }
    }
    std::string text_;
};

using Store = chat::BasicChatStore<ThrowOnNthCopy>;

// Пять комнат, в каждой по одному старому сообщению.
const std::vector<std::string_view>& five_rooms() {
    static const std::vector<std::string_view> rooms = {"#a", "#b", "#c", "#d", "#e"};
    return rooms;
}

Store make_store() {
    Store store;
    for (const std::string_view room : five_rooms()) {
        store.create_room(std::string(room));
        store.post(room, ThrowOnNthCopy("old"));
    }
    return store;
}

// Сколько сообщений в каждой из пяти комнат.
std::vector<std::size_t> sizes(const Store& store) {
    std::vector<std::size_t> result;
    for (const std::string_view room : five_rooms()) {
        result.push_back(store.history(room)->get().size());
    }
    return result;
}

}  // namespace

TEST(Guarantee, PostManyIsAllOrNothing) {
    // Рассылка в пять комнат делает четыре копирования: пятой комнате
    // достаётся оригинал. Бросок перебирается на каждом — на первой комнате,
    // в середине списка и на последней копии. Одна точка броска доказывала бы
    // гарантию только для неё.
    constexpr int copies_in_successful_post = 4;
    for (int nth = 1; nth <= copies_in_successful_post; ++nth) {
        Store store = make_store();

        countdown() = nth;
        bool thrown = false;
        try {
            store.post_many(five_rooms(), ThrowOnNthCopy("new"));
        } catch (const std::bad_alloc&) {
            thrown = true;
        }
        countdown() = -1;

        ASSERT_TRUE(thrown) << "copy " << nth;
        // Ни в одну комнату ничего не добавилось — даже в первые, чья копия
        // уже была готова к моменту броска.
        EXPECT_EQ(sizes(store), std::vector<std::size_t>(5, 1)) << "copy " << nth;
    }

    // Пятого копирования нет: перебор выше покрыл все точки броска.
    Store store = make_store();
    countdown() = copies_in_successful_post + 1;
    EXPECT_NO_THROW(store.post_many(five_rooms(), ThrowOnNthCopy("new")));
    countdown() = -1;
    EXPECT_EQ(sizes(store), std::vector<std::size_t>(5, 2));
}

TEST(Guarantee, UnknownRoomInTheMiddleChangesNothing) {
    Store store = make_store();
    const std::vector<std::string_view> rooms = {"#a", "#b", "#absent", "#d"};

    EXPECT_THROW(store.post_many(rooms, ThrowOnNthCopy("new")), chat::UnknownRoomError);
    EXPECT_EQ(sizes(store), std::vector<std::size_t>(5, 1));
}

TEST(Guarantee, EveryRoomGetsItsOwnCopy) {
    Store store = make_store();
    store.post_many(five_rooms(), ThrowOnNthCopy("new"));
    for (const std::string_view room : five_rooms()) {
        EXPECT_EQ(store.history(room)->get().back().text(), std::string("new")) << room;
    }
}

TEST(Guarantee, CommitNeedsNoexceptMove) {
    // Фиксация переносит копии перемещением. Если бы оно могло бросить,
    // фиксация перестала бы быть точкой невозврата. post_many проверяет это
    // static_assert; здесь — что настоящий элемент истории условию
    // удовлетворяет.
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<chat::MessagePtr>);
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<ThrowOnNthCopy>);
}

// --- Этап 4: та же гарантия на настоящих полиморфных сообщениях ------------
//
// После перехода на unique_ptr копии сообщения для рассылки делает clone.
// Тест подставляет сообщение, чей clone бросает на N-м вызове, и повторяет
// перебор точек броска уже на настоящем ChatStore.

namespace {

int& clone_countdown() {
    static int n = -1;
    return n;
}

class ThrowingClone final : public chat::IMessage {
public:
    explicit ThrowingClone(std::string text)
        : IMessage("tester", chat::Timestamp{}), text_(std::move(text)) {}
    ThrowingClone(const ThrowingClone&) = default;
    ThrowingClone& operator=(const ThrowingClone&) = delete;

    [[nodiscard]] std::string_view type_name() const noexcept override { return "throwing"; }
    [[nodiscard]] std::string to_string() const override { return text_; }
    [[nodiscard]] std::size_t size() const noexcept override { return text_.size(); }
    [[nodiscard]] chat::MessagePtr clone() const override {
        if (clone_countdown() > 0 && --clone_countdown() == 0) {
            throw std::bad_alloc();
        }
        return std::make_unique<ThrowingClone>(*this);
    }

private:
    std::string text_;
};

}  // namespace

TEST(Guarantee, PostManyWithPolymorphicMessages) {
    constexpr int clones_in_successful_post = 4;   // пять комнат, оригинал — последней
    for (int nth = 1; nth <= clones_in_successful_post; ++nth) {
        chat::ChatStore store;
        for (const std::string_view room : five_rooms()) {
            store.create_room(std::string(room));
            store.post(room, chat::make_text("old", "old"));
        }

        clone_countdown() = nth;
        EXPECT_THROW(store.post_many(five_rooms(), std::make_unique<ThrowingClone>("new")),
                     std::bad_alloc)
            << "clone " << nth;
        clone_countdown() = -1;

        for (const std::string_view room : five_rooms()) {
            EXPECT_EQ(store.history(room)->get().size(), 1u) << room << ", clone " << nth;
        }
    }
}
