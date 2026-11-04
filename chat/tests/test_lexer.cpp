#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "chat/lexer.hpp"

using chat::tokenize;

TEST(Lexer, SplitsBySpaces) {
    const std::string line = "/msg #general hello";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], std::string_view("/msg"));
    EXPECT_EQ(tokens[1], std::string_view("#general"));
}

TEST(Lexer, TokensPointIntoSourceWithoutCopying) {
    const std::string line = "/msg #general hello";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[1].data(), line.data() + 5);
}

TEST(Lexer, TailIsTakenWholeWhenLimitReached) {
    // Особенность чата: текст сообщения приходит остатком строки.
    const std::string line = "/msg #general всем привет, как дела";
    const auto tokens = tokenize(line, 3);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[2], std::string_view("всем привет, как дела"));
}

TEST(Lexer, HandlesQuotedArgument) {
    const std::string line = R"(/topic #general "курс по C++")";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[2], std::string_view("курс по C++"));
}

TEST(Lexer, UnterminatedQuoteYieldsNothing) {
    const std::string line = R"(/topic #general "курс)";
    EXPECT_TRUE(tokenize(line).empty());
}

TEST(Lexer, IgnoresExtraSpaces) {
    const std::string line = "   /join    #general   ";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], std::string_view("/join"));
    EXPECT_EQ(tokens[1], std::string_view("#general"));
}

TEST(Lexer, TailKeepsInnerSpacesButDropsLeadingOnes) {
    const std::string line = "/msg   #general    два  пробела внутри";
    const auto tokens = tokenize(line, 3);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[1], std::string_view("#general"));
    EXPECT_EQ(tokens[2], std::string_view("два  пробела внутри"));
}

TEST(Lexer, EmptyLineYieldsNoTokens) {
    const std::string empty;
    const std::string spaces = "     ";
    EXPECT_TRUE(tokenize(empty).empty());
    EXPECT_TRUE(tokenize(spaces).empty());
}
