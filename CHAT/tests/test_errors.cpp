#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <type_traits>

#include "errors.hpp"

TEST(Errors, HierarchyIsRootedAtChatError) {
    EXPECT_TRUE((std::is_base_of_v<std::runtime_error, ChatError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, ParseError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, UnknownRoomError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, NicknameTakenError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, TypeMismatchError>));
}

TEST(Errors, ContextIsInFieldsNotOnlyInMessage) {
    const TypeMismatchError e("#general", "reaction", "text");
    EXPECT_EQ(e.room(), std::string("#general"));
    EXPECT_EQ(e.expected(), std::string("reaction"));
    EXPECT_EQ(e.actual(), std::string("text"));
}

TEST(Errors, CatchingByBaseReferenceKeepsDerivedType) {
    try {
        throw NicknameTakenError("alice");
    } catch (const ChatError& e) {
        const auto* derived = dynamic_cast<const NicknameTakenError*>(&e);
        ASSERT_NE(derived, nullptr);
        EXPECT_EQ(derived->nick(), std::string("alice"));
    }
}
