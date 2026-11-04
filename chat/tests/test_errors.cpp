#include <gtest/gtest.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "chat/errors.hpp"

using chat::ChatError;

TEST(Errors, HierarchyIsRootedAtChatError) {
    // Одна ветка catch (const ChatError&) ловит любую ошибку проекта, а
    // catch (const std::runtime_error&) — любую ошибку вообще.
    EXPECT_TRUE((std::is_base_of_v<std::runtime_error, ChatError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, chat::ParseError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, chat::UnknownRoomError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, chat::NotMemberError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, chat::NicknameTakenError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, chat::MessageNotFoundError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, chat::TypeMismatchError>));
    EXPECT_TRUE((std::is_base_of_v<ChatError, chat::CounterOverflowError>));
}

TEST(Errors, ContextIsInFieldsNotOnlyInMessage) {
    // На этапе 16 код ответа выбирается по полям, а не по разбору what().
    const chat::NicknameTakenError taken("alice");
    EXPECT_EQ(taken.nick(), std::string("alice"));

    const chat::TypeMismatchError mismatch("#general", 3, "reaction", "text");
    EXPECT_EQ(mismatch.room(), std::string("#general"));
    EXPECT_EQ(mismatch.index(), 3u);
    EXPECT_EQ(mismatch.expected(), std::string("reaction"));
    EXPECT_EQ(mismatch.actual(), std::string("text"));

    const chat::NotMemberError outsider("#general", "bob");
    EXPECT_EQ(outsider.room(), std::string("#general"));
    EXPECT_EQ(outsider.nick(), std::string("bob"));
}

TEST(Errors, CatchingByBaseReferenceKeepsDerivedType) {
    try {
        throw chat::UnknownRoomError("#absent");
    } catch (const ChatError& e) {
        // Поймали по ссылке на базу — динамический тип сохранился.
        const auto* const derived = dynamic_cast<const chat::UnknownRoomError*>(&e);
        ASSERT_NE(derived, nullptr);
        EXPECT_EQ(derived->room(), std::string("#absent"));
    }
}

TEST(Errors, MessageMentionsContext) {
    const chat::ParseError e("unknown command: /frob", "/frob #general");
    const std::string what = e.what();
    EXPECT_NE(what.find("unknown command"), std::string::npos);
    EXPECT_NE(what.find("/frob #general"), std::string::npos);
}

// Звёздочка: цепочка из трёх звеньев разворачивается в причину и контекст.
TEST(Errors, NestedChainUnwindsToRootCause) {
    std::exception_ptr chain;
    try {
        try {
            try {
                throw chat::UnknownRoomError("#absent");
            } catch (...) {
                std::throw_with_nested(ChatError("while posting to #absent"));
            }
        } catch (...) {
            std::throw_with_nested(ChatError("while executing /msg #absent"));
        }
    } catch (...) {
        chain = std::current_exception();
    }

    try {
        std::rethrow_exception(chat::root_cause(chain));
    } catch (const chat::UnknownRoomError& e) {
        EXPECT_EQ(e.room(), std::string("#absent"));
    } catch (...) {
        ADD_FAILURE() << "root cause has the wrong type";
    }

    const std::vector<std::string> context = chat::context_of(chain);
    ASSERT_EQ(context.size(), 2u);
    EXPECT_EQ(context[0], std::string("while executing /msg #absent"));
    EXPECT_EQ(context[1], std::string("while posting to #absent"));
}

TEST(Errors, PlainExceptionIsItsOwnRootCause) {
    const std::exception_ptr single = std::make_exception_ptr(chat::ParseError("empty command", ""));
    EXPECT_EQ(chat::root_cause(single), single);
    EXPECT_TRUE(chat::context_of(single).empty());
}
