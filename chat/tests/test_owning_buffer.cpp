#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

#include "chat/owning_buffer.hpp"

using chat::OwningBuffer;

TEST(OwningBuffer, CopyIsIndependent) {
    const OwningBuffer original{std::string("payload")};
    OwningBuffer copy{original};

    ASSERT_EQ(copy.size(), original.size());
    EXPECT_NE(copy.data(), original.data());   // память разная, а не общая

    // Домашнее задание: изменение копии не видно в оригинале.
    copy.data()[0] = 'P';
    EXPECT_EQ(copy.str(), std::string("Payload"));
    EXPECT_EQ(original.str(), std::string("payload"));
}

TEST(OwningBuffer, MoveTransfersOwnership) {
    OwningBuffer source{std::string("payload")};
    const char* const address = source.data();

    const OwningBuffer target{std::move(source)};

    EXPECT_EQ(target.data(), address);   // буфер переехал, а не скопировался
    EXPECT_EQ(source.data(), nullptr);   // источник пригоден для разрушения
    EXPECT_EQ(source.size(), 0u);
}

// Домашнее задание: перемещённый объект остаётся в корректном состоянии —
// его можно не только разрушить, но и снова использовать.
TEST(OwningBuffer, MovedFromObjectIsReusable) {
    OwningBuffer source{std::string("first")};
    const OwningBuffer target{std::move(source)};

    EXPECT_TRUE(source.empty());
    EXPECT_EQ(source.str(), std::string());

    source = OwningBuffer{std::string("second")};
    EXPECT_EQ(source.str(), std::string("second"));
    EXPECT_EQ(target.str(), std::string("first"));

    const OwningBuffer copy_of_empty{OwningBuffer{}};
    EXPECT_TRUE(copy_of_empty.empty());
}

TEST(OwningBuffer, SelfAssignmentIsSafe) {
    OwningBuffer buffer{std::string("payload")};

    // Классическая ловушка: реализация «сначала освободили, потом скопировали
    // из себя же» разрушила бы объект. Идиома copy-and-swap делает копию
    // до того, как трогает состояние, поэтому случай безопасен сам собой.
    const OwningBuffer& alias = buffer;
    buffer = alias;

    EXPECT_EQ(buffer.size(), 7u);
    EXPECT_EQ(buffer.str(), std::string("payload"));
}

TEST(OwningBuffer, SelfMoveAssignmentKeepsObjectValid) {
    OwningBuffer buffer{std::string("payload")};
    OwningBuffer& alias = buffer;

    buffer = std::move(alias);

    // Стандарт требует от перемещённого объекта лишь пригодности к
    // разрушению и присваиванию; здесь содержимое даже сохраняется.
    EXPECT_EQ(buffer.str(), std::string("payload"));
}

TEST(OwningBuffer, CopyAssignmentReplacesContent) {
    OwningBuffer target{std::string("old")};
    const OwningBuffer source{std::string("new-content")};

    target = source;

    EXPECT_EQ(target.str(), std::string("new-content"));
    EXPECT_NE(target.data(), source.data());
}

TEST(OwningBuffer, MoveAssignmentStealsBuffer) {
    OwningBuffer target{std::string("old")};
    OwningBuffer source{std::string("new-content")};
    const char* const address = source.data();

    target = std::move(source);

    EXPECT_EQ(target.data(), address);
    EXPECT_TRUE(source.empty());
}

TEST(OwningBuffer, MoveOperationsAreNoexcept) {
    // Без noexcept vector при перевыделении копировал бы вместо перемещения.
    EXPECT_TRUE(std::is_nothrow_move_constructible<OwningBuffer>::value);
    EXPECT_TRUE(std::is_nothrow_move_assignable<OwningBuffer>::value);
}

TEST(OwningBuffer, EmptyBufferIsValid) {
    const OwningBuffer empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.data(), nullptr);

    const OwningBuffer copy{empty};
    EXPECT_TRUE(copy.empty());
}
