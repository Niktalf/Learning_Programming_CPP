// Этап 5: концепции проверяются не только на принятие, но и на отказ.

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/concepts.hpp"
#include "chat/message.hpp"
#include "chat/message_id.hpp"
#include "tracked.hpp"

namespace {

// to_bytes есть, но тип результата неверный: проверка «метод есть»
// пропустила бы, проверка типа результата — нет.
struct WrongResult {
    [[nodiscard]] int to_bytes() const { return 0; }
    static WrongResult from_bytes(std::span<const std::byte>) { return {}; }
};

struct WrongFactory {
    [[nodiscard]] std::vector<std::byte> to_bytes() const { return {}; }
    static int from_bytes(std::span<const std::byte>) { return 0; }
};

// Перемещение бросает — фиксация рассылки перестала бы быть безопасной.
struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(const ThrowingMove&) = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};

}  // namespace

TEST(Concepts, NameLike) {
    EXPECT_TRUE(chat::NameLike<std::string>);
    EXPECT_TRUE(chat::NameLike<std::string_view>);
    EXPECT_TRUE(chat::NameLike<const std::string&>);
    EXPECT_TRUE(chat::NameLike<decltype("#general")>);

    EXPECT_FALSE(chat::NameLike<int>);
    // Указатель без длины — нет: string_view из нулевого указателя —
    // неопределённое поведение. convertible_to<string_view> его бы пропустил.
    EXPECT_FALSE(chat::NameLike<const char*>);
    EXPECT_FALSE(chat::NameLike<std::nullptr_t>);
}

TEST(Concepts, NameViewOfLiteralUsesArrayLength) {
    EXPECT_EQ(chat::name_view("#general"), std::string_view("#general"));
    EXPECT_EQ(chat::name_view("#general").size(), 8u);
}

TEST(Concepts, StoreAcceptsEveryNameLike) {
    chat::ChatStore store;
    store.create_room("#general");
    const std::string owned = "#general";
    EXPECT_TRUE(store.contains("#general"));
    EXPECT_TRUE(store.contains(owned));
    EXPECT_TRUE(store.contains(std::string_view(owned)));
}

TEST(Concepts, HistoryItem) {
    EXPECT_TRUE(chat::HistoryItem<chat::MessagePtr>);   // копия через clone
    EXPECT_TRUE(chat::HistoryItem<chat::testing::Tracked>);
    EXPECT_FALSE(chat::HistoryItem<ThrowingMove>);
    // Голый IMessage — абстрактный, ни скопировать, ни переместить.
    EXPECT_FALSE(chat::HistoryItem<chat::IMessage>);
}

TEST(Concepts, SerializableChecksResultTypes) {
    EXPECT_TRUE(chat::Serializable<chat::MessageId>);
    EXPECT_FALSE(chat::Serializable<WrongResult>);
    EXPECT_FALSE(chat::Serializable<WrongFactory>);
    EXPECT_FALSE(chat::Serializable<chat::PlainMessageId>);
}

TEST(Concepts, MessageIdRoundTrip) {
    const chat::MessageId id{7, 1700000000000, "alice"};
    const chat::MessageId restored = chat::MessageId::from_bytes(id.to_bytes());
    EXPECT_EQ(restored, id);
    EXPECT_EQ(restored.author, std::string("alice"));

    std::vector<std::byte> bytes = id.to_bytes();
    bytes.pop_back();
    EXPECT_THROW(static_cast<void>(chat::MessageId::from_bytes(bytes)), chat::ParseError);
}
