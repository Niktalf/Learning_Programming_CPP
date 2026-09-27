#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "lexer.hpp"


TEST(Lexer, SplitsBySpaces) {
    const std::string line = "POLL build now";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], std::string_view("POLL"));
    EXPECT_EQ(tokens[1], std::string_view("build"));
}

TEST(Lexer, TokensPointIntoSourceWithoutCopying) {
    const std::string line = "POLL build now";
    const auto tokens = tokenize(line);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[1].data(), line.data() + 5);
}

TEST(Lexer, TailIsTakenWholeWhenLimitReached) {
    const std::string line = "SUBMIT build 5 make -j8 all";
    const auto tokens = tokenize(line, 4);

    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[3], std::string_view("make -j8 all"));
}

TEST(Lexer, HandlesQuotedArgument) {
    const std::string line = R"(SUBMIT "build tasks" 5 make all)";
    const auto tokens = tokenize(line, 4);

    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[1], std::string_view("build tasks"));
}

TEST(Lexer, UnterminatedQuoteYieldsNothing) {
    const std::string line = R"(SUBMIT "build 5 make)";
    EXPECT_TRUE(tokenize(line).empty());
}

TEST(Lexer, EmptyLineYieldsNoTokens) {
    const std::string empty;
    const std::string spaces = "     ";
    EXPECT_TRUE(tokenize(empty).empty());
    EXPECT_TRUE(tokenize(spaces).empty());
}
