#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>

#include "lexer.hpp"
#include "parser.hpp"

namespace {
    template <class T, class = void>
    struct tokenize_accepts : std::false_type {};

    template <class T>
    struct tokenize_accepts<T, std::void_t<decltype(tokenize(std::declval<T>()))>> : std::true_type {};

    template <class T, class = void>
    struct parse_accepts : std::false_type {};

    template <class T>
    struct parse_accepts<T, std::void_t<decltype(parse(std::declval<T>()))>> : std::true_type {};
}  // namespace

TEST(Lifetime, TokenizeRejectsTemporaryString) {
    EXPECT_TRUE(tokenize_accepts<std::string_view>::value);
    EXPECT_FALSE(tokenize_accepts<std::string&&>::value);
}

TEST(Lifetime, ParseRejectsTemporaryInputLine) {
    EXPECT_TRUE(parse_accepts<const InputLine&>::value);
    EXPECT_FALSE(parse_accepts<InputLine&&>::value);
}

TEST(Lifetime, InputLineIsNeitherCopyableNorMovable) {
    EXPECT_FALSE(std::is_copy_constructible_v<InputLine>);
    EXPECT_FALSE(std::is_move_constructible_v<InputLine>);
}
