// Иерархия сообщений: виртуальный деструктор, clone, проверка вида.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "chat/message.hpp"

using chat::IMessage;
using chat::MessagePtr;

namespace {

int& destroyed() {
    static int count = 0;
    return count;
}

// Наследник, считающий свои разрушения: через него видно, что
// unique_ptr<IMessage> вызывает деструктор наследника, а не только базы.
class CountingMessage final : public IMessage {
public:
    CountingMessage() : IMessage("counter", chat::Timestamp{}) {}
    CountingMessage(const CountingMessage&) = default;
    CountingMessage& operator=(const CountingMessage&) = delete;
    ~CountingMessage() override { ++destroyed(); }

    [[nodiscard]] std::string_view type_name() const noexcept override { return "counting"; }
    [[nodiscard]] std::string to_string() const override { return "counting"; }
    [[nodiscard]] std::size_t size() const noexcept override { return 0; }
    [[nodiscard]] MessagePtr clone() const override {
        return std::make_unique<CountingMessage>(*this);
    }
};

}  // namespace

TEST(Message, DestructorIsVirtual) {
    // Без virtual удаление через указатель на базу — неопределённое
    // поведение, и unique_ptr от него не защищает.
    EXPECT_TRUE(std::has_virtual_destructor_v<IMessage>);
    EXPECT_TRUE(std::is_abstract_v<IMessage>);
}

TEST(Message, DeletingThroughBaseRunsDerivedDestructor) {
    destroyed() = 0;
    {
        const MessagePtr message = std::make_unique<CountingMessage>();
        static_cast<void>(message);
    }
    EXPECT_EQ(destroyed(), 1);
}

TEST(Message, KindIsDeterminedByTheClass) {
    const MessagePtr text = chat::make_text("alice", "hello");
    const MessagePtr notice = chat::make_notice("alice", "alice joined");
    const MessagePtr reaction = chat::make_reaction("bob", "fire");

    EXPECT_EQ(text->type_name(), std::string_view("text"));
    EXPECT_EQ(notice->type_name(), std::string_view("notice"));
    EXPECT_EQ(reaction->type_name(), std::string_view("reaction"));

    EXPECT_EQ(text->author(), std::string("alice"));
    EXPECT_EQ(text->to_string(), std::string("hello"));
    EXPECT_EQ(notice->to_string(), std::string("* alice joined"));
    EXPECT_EQ(reaction->to_string(), std::string(":fire: x1"));

    // Проверка вида — dynamic_cast: указатель или nullptr. Роль get_if у variant.
    EXPECT_EQ(dynamic_cast<const chat::ReactionMessage*>(text.get()), nullptr);
    EXPECT_NE(dynamic_cast<const chat::ReactionMessage*>(reaction.get()), nullptr);
}

TEST(Message, CloneIsDeepAndKeepsTheKind) {
    const MessagePtr original = chat::make_reaction("bob", "fire");
    const MessagePtr copy = original->clone();
    ASSERT_NE(copy.get(), original.get());

    auto* const copied = dynamic_cast<chat::ReactionMessage*>(copy.get());
    ASSERT_NE(copied, nullptr);
    copied->set_count(5);
    // Изменение копии не видно в оригинале.
    EXPECT_EQ(dynamic_cast<const chat::ReactionMessage&>(*original).count(), 1);
    EXPECT_EQ(copy->author(), std::string("bob"));
}

TEST(Message, DuplicateUsesCloneForPointers) {
    // duplicate — то, чем рассылка делает копии для каждой комнаты.
    const MessagePtr original = chat::make_text("alice", "hi");
    const MessagePtr copy = chat::duplicate(original);
    EXPECT_NE(copy.get(), original.get());
    EXPECT_EQ(copy->to_string(), std::string("hi"));

    // Для копируемых типов — обычное копирование.
    EXPECT_EQ(chat::duplicate(std::string("plain")), std::string("plain"));
}

TEST(Message, BaseCannotBeCopiedFromOutside) {
    EXPECT_FALSE(std::is_copy_assignable_v<IMessage>);
    // Указатель на сообщение — только перемещается.
    EXPECT_FALSE(std::is_copy_constructible_v<MessagePtr>);
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<MessagePtr>);
}
