// Проверки ввода, найденные фаззером (tests/test_fuzz.cpp). Каждая находка
// закрыта здесь отдельным тестом.

#include <gtest/gtest.h>

#include <exception>
#include <string>

#include "chat/commands.hpp"
#include "chat/errors.hpp"
#include "chat/validate.hpp"

TEST(Validate, Utf8AcceptsWellFormedText) {
    EXPECT_TRUE(chat::is_valid_utf8(""));
    EXPECT_TRUE(chat::is_valid_utf8("hello"));
    EXPECT_TRUE(chat::is_valid_utf8("привет"));
    EXPECT_TRUE(chat::is_valid_utf8("\xe2\x82\xac"));       // €, три байта
    EXPECT_TRUE(chat::is_valid_utf8("\xf0\x9f\x98\x80"));   // эмодзи, четыре байта
}

TEST(Validate, Utf8RejectsBrokenSequences) {
    EXPECT_FALSE(chat::is_valid_utf8("\xd0"));               // обрезано посреди символа
    EXPECT_FALSE(chat::is_valid_utf8("\xe2\x82"));
    EXPECT_FALSE(chat::is_valid_utf8("\x80"));               // продолжение без начала
    EXPECT_FALSE(chat::is_valid_utf8("\xc0\xaf"));           // избыточная форма '/'
    EXPECT_FALSE(chat::is_valid_utf8("\xe0\x80\xaf"));       // то же, трёхбайтовая
    EXPECT_FALSE(chat::is_valid_utf8("\xed\xa0\x80"));       // суррогат
    EXPECT_FALSE(chat::is_valid_utf8("\xf4\x90\x80\x80"));   // больше U+10FFFF
    EXPECT_FALSE(chat::is_valid_utf8("\xff"));
}

TEST(Validate, RoomNames) {
    EXPECT_FALSE(chat::room_name_problem("#general").has_value());
    EXPECT_FALSE(chat::room_name_problem("#cpp-2026_spring").has_value());
    EXPECT_TRUE(chat::room_name_problem("general").has_value());   // без решётки
    EXPECT_TRUE(chat::room_name_problem("#").has_value());
    EXPECT_TRUE(chat::room_name_problem("# ").has_value());        // в кавычках прошло бы
    EXPECT_TRUE(chat::room_name_problem("#a b").has_value());
    EXPECT_TRUE(chat::room_name_problem("#" + std::string(33, 'r')).has_value());
}

TEST(Validate, Nicknames) {
    EXPECT_FALSE(chat::nick_problem("alice").has_value());
    EXPECT_TRUE(chat::nick_problem("").has_value());
    EXPECT_TRUE(chat::nick_problem("   ").has_value());   // ник из одних пробелов
    EXPECT_TRUE(chat::nick_problem(std::string(33, 'n')).has_value());
    EXPECT_TRUE(chat::nick_problem("al\x1b[31mice").has_value());
}

TEST(Validate, TextWithoutControlCharacters) {
    EXPECT_FALSE(chat::text_problem("hello, world").has_value());
    EXPECT_TRUE(chat::text_problem("   ").has_value());
    // ESC-последовательность очистила бы терминал другим участникам.
    EXPECT_TRUE(chat::text_problem("\x1b[2Jgotcha").has_value());
    EXPECT_TRUE(chat::text_problem(std::string("a\0b", 3)).has_value());
}

TEST(Validate, ParserRejectsWhatTheFuzzerFound) {
    // С этапа 6 разбирает реестр; ошибку аргумента он оборачивает
    // подсказкой, поэтому причина — по корню цепочки.
    const auto reason = [](const std::string& text) {
        try {
            static_cast<void>(chat::commands().bind(text));
        } catch (...) {
            try {
                std::rethrow_exception(chat::root_cause(std::current_exception()));
            } catch (const chat::ParseError& e) {
                return e.reason();
            } catch (...) {
                return std::string("not a parse error");
            }
        }
        return std::string("parsed");
    };
    EXPECT_EQ(reason("/msg #general " + std::string(1 << 20, 'x')),
              std::string("line too long: 1048590 bytes, limit is 4096"));
    EXPECT_EQ(reason("/msg #general \xd0"), std::string("invalid UTF-8"));
    EXPECT_EQ(reason(R"(/join "# ")"), std::string("room name may contain only letters, digits, _ and -"));
    EXPECT_EQ(reason("/msg #general \x1b[2J"), std::string("text contains control characters"));
    EXPECT_EQ(reason(R"(/topic #general "   ")"), std::string("text is empty"));
}
