// Этап 5: концепции проверяются не только на принятие, но и на отказ.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "kvs/bytes.hpp"
#include "kvs/concepts.hpp"
#include "kvs/ivalue.hpp"
#include "kvs/store.hpp"
#include "kvs/value.hpp"
#include "kvs/version.hpp"
#include "tracked.hpp"

namespace {

// Тип с to_bytes, но неверным типом результата: проверка «метод есть»
// его бы пропустила, проверка типа результата — нет.
struct WrongResult {
    [[nodiscard]] int to_bytes() const { return 0; }
    static WrongResult from_bytes(std::span<const std::byte>) { return {}; }
};

// from_bytes возвращает не тот тип.
struct WrongFactory {
    [[nodiscard]] std::vector<std::byte> to_bytes() const { return {}; }
    static int from_bytes(std::span<const std::byte>) { return 0; }
};

// Значение без size — хранилище не сможет учесть его объём.
struct NoSize {};

// Значение с бросающим перемещением — вектор и обмен перестанут быть
// безопасными.
struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(const ThrowingMove&) = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
    [[nodiscard]] std::size_t size() const { return 0; }
};

}  // namespace

TEST(Concepts, KeyLike) {
    EXPECT_TRUE(kvs::KeyLike<std::string>);
    EXPECT_TRUE(kvs::KeyLike<std::string_view>);
    EXPECT_TRUE(kvs::KeyLike<const std::string&>);
    EXPECT_TRUE(kvs::KeyLike<decltype("literal")>);   // const char(&)[8]

    EXPECT_FALSE(kvs::KeyLike<int>);
    EXPECT_FALSE(kvs::KeyLike<std::int64_t>);
    // Указатель без длины — нет: string_view из нулевого указателя —
    // неопределённое поведение. convertible_to<string_view> его бы пропустил.
    EXPECT_FALSE(kvs::KeyLike<const char*>);
    EXPECT_FALSE(kvs::KeyLike<std::nullptr_t>);
    EXPECT_FALSE(kvs::KeyLike<std::vector<char>>);
}

TEST(Concepts, KeyViewOfLiteralUsesArrayLength) {
    EXPECT_EQ(kvs::key_view("user:1"), std::string_view("user:1"));
    EXPECT_EQ(kvs::key_view("user:1").size(), 6u);   // без завершающего нуля
}

TEST(Concepts, StoreAcceptsEveryKeyLike) {
    kvs::Store store;
    store.set("k", kvs::Value(std::string("v")));
    const std::string owned = "k";
    const std::string_view view = owned;
    EXPECT_TRUE(store.contains("k"));
    EXPECT_TRUE(store.contains(owned));
    EXPECT_TRUE(store.contains(view));
}

TEST(Concepts, StorableValue) {
    EXPECT_TRUE(kvs::StorableValue<kvs::Value>);
    EXPECT_TRUE(kvs::StorableValue<kvs::testing::Tracked>);
    EXPECT_FALSE(kvs::StorableValue<NoSize>);
    EXPECT_FALSE(kvs::StorableValue<ThrowingMove>);
    EXPECT_FALSE(kvs::StorableValue<std::unique_ptr<kvs::IValue>>);   // не копируется
}

TEST(Concepts, SerializableChecksResultTypes) {
    EXPECT_TRUE(kvs::Serializable<kvs::RecordVersion>);
    EXPECT_TRUE(kvs::Serializable<kvs::IntValue>);
    EXPECT_TRUE(kvs::Serializable<kvs::StringValue>);
    EXPECT_FALSE(kvs::Serializable<WrongResult>);
    EXPECT_FALSE(kvs::Serializable<WrongFactory>);
    EXPECT_FALSE(kvs::Serializable<kvs::Value>);   // сериализация — у конкретных классов
}

TEST(Concepts, SerializableRoundTrip) {
    const kvs::RecordVersion version{3, 17, "alice"};
    const auto restored = kvs::decode<kvs::RecordVersion>(kvs::encode(version));
    EXPECT_EQ(restored, version);
    EXPECT_EQ(restored.author, std::string("alice"));

    const kvs::IntValue number(-42);
    EXPECT_EQ(kvs::decode<kvs::IntValue>(kvs::encode(number)).data(), -42);

    const kvs::StringValue text("hello");
    EXPECT_EQ(kvs::decode<kvs::StringValue>(kvs::encode(text)).data(), std::string("hello"));
}

TEST(Concepts, TruncatedBytesAreParseError) {
    std::vector<std::byte> bytes = kvs::encode(kvs::RecordVersion{1, 2, "bob"});
    bytes.pop_back();
    EXPECT_THROW(static_cast<void>(kvs::decode<kvs::RecordVersion>(bytes)), kvs::ParseError);
}
