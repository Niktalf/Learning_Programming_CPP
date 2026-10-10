#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "lexer.hpp"
#include "errors.hpp"

TEST(Lexer, SplitsBySpaces) {
    const std::string line = "SET key value";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], std::string_view("SET"));
    EXPECT_EQ(tokens[1], std::string_view("key"));
    EXPECT_EQ(tokens[2], std::string_view("value"));
}

TEST(Lexer, TokensPointIntoSourceWithoutCopying) {
    const std::string line = "SET key value";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[1].data(), line.data() + 4);
}

TEST(Lexer, HandlesQuotedArgument) {
    const std::string line = R"(SET greeting "hello world")";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[2], std::string_view("hello world"));
}

TEST(Lexer, UnterminatedQuoteIsAnError) {
    const std::string line = R"(SET greeting "hello)";
    EXPECT_THROW((void)tokenize(line), ParseError);
}

TEST(Lexer, IgnoresExtraSpaces) {
    const std::string line = "   SET    key     value   ";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[2], std::string_view("value"));
}

TEST(Lexer, EmptyLineYieldsNoTokens) {
    const std::string empty;
    const std::string spaces = "     ";
    EXPECT_TRUE(tokenize(empty).empty());
    EXPECT_TRUE(tokenize(spaces).empty());
}
