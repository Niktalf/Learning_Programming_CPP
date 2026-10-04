#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "lexer.hpp"
#include "errors.hpp"

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
    const std::string line = "/msg #general hi everyone, how are you";
    const auto tokens = tokenize(line, 3);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[2], std::string_view("hi everyone, how are you"));
}

TEST(Lexer, HandlesQuotedArgument) {
    const std::string line = R"(/topic #general "C++ course")";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[2], std::string_view("C++ course"));
}

TEST(Lexer, UnterminatedQuoteIsAnError) {
    const std::string line = R"(/topic #general "course)";

    EXPECT_THROW((void)tokenize(line), ParseError);
}

TEST(Lexer, EmptyLineYieldsNoTokens) {
    const std::string empty;
    const std::string spaces = "     ";

    EXPECT_TRUE(tokenize(empty).empty());
    EXPECT_TRUE(tokenize(spaces).empty());
}
