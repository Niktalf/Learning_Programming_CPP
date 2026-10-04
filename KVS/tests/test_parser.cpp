#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include "errors.hpp"
#include "parser.hpp"

TEST(Parser, ParsesSet) {
    const InputLine line{"SET key value"};
    const auto command = parse(line);

    ASSERT_TRUE(std::holds_alternative<SetCommand>(command));
    const auto& set = std::get<SetCommand>(command);
    EXPECT_EQ(set.key, std::string_view("key"));
    EXPECT_EQ(set.value, std::string_view("value"));
}

TEST(Parser, ParsesGetAndDel) {
    const InputLine get_line{"GET key"};
    const InputLine del_line{"DEL key"};

    EXPECT_TRUE(std::holds_alternative<GetCommand>(parse(get_line)));
    EXPECT_TRUE(std::holds_alternative<DelCommand>(parse(del_line)));
}

TEST(Parser, VerbIsCaseInsensitive) {
    const InputLine line{"get key"};
    EXPECT_NO_THROW((void)parse(line));
}

TEST(Parser, IncrDefaultsToOne) {
    const InputLine line{"INCR counter"};
    const auto command = parse(line);

    ASSERT_TRUE(std::holds_alternative<IncrCommand>(command));
    EXPECT_EQ(std::get<IncrCommand>(command).by, 1);
}

TEST(Parser, IncrRejectsTrailingGarbage) {
    const InputLine line{"INCR counter 12abc"};
    EXPECT_THROW((void)parse(line), ParseError);
}

TEST(Parser, RejectsWrongArity) {
    const InputLine too_few{"SET key"};
    const InputLine too_many{"GET a b"};

    EXPECT_THROW((void)parse(too_few), ParseError);
    EXPECT_THROW((void)parse(too_many), ParseError);
}

TEST(Parser, RejectsUnknownVerb) {
    const InputLine line{"FLUSHALL"};
    EXPECT_THROW((void)parse(line), ParseError);
}

TEST(Parser, ResultBorrowsFromInputLine) {
    const InputLine line{"SET key value"};
    const auto command = parse(line);

    const auto& [key, value] = std::get<SetCommand>(command);
    EXPECT_EQ(key.data(), line.view().data() + 4);
}

TEST(Parser, ParsesMsetPairs) {
    const InputLine line{"MSET a 1 b 2"};
    const auto command = parse(line);

    ASSERT_TRUE(std::holds_alternative<MsetCommand>(command));
    const auto& [pairs] = std::get<MsetCommand>(command);
    ASSERT_EQ(pairs.size(), 2u);
    EXPECT_EQ(pairs[1].first, std::string_view("b"));
    EXPECT_EQ(pairs[1].second, std::string_view("2"));
}

TEST(Parser, MsetRejectsOddArgumentCount) {
    const InputLine line{"MSET a 1 b"};
    EXPECT_THROW((void)parse(line), ParseError);
}
