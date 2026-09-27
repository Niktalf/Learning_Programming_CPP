#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include "parser.hpp"

TEST(Parser, ParsesSet) {
    const InputLine line{"SET key value"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    ASSERT_TRUE(std::holds_alternative<SetCommand>(*command));
    const auto& set = std::get<SetCommand>(*command);
    EXPECT_EQ(set.key, std::string_view("key"));
    EXPECT_EQ(set.value, std::string_view("value"));
}

TEST(Parser, ParsesGetAndDel) {
    const InputLine get_line{"GET key"};
    const InputLine del_line{"DEL key"};

    EXPECT_TRUE(std::holds_alternative<GetCommand>(*parse(get_line)));
    EXPECT_TRUE(std::holds_alternative<DelCommand>(*parse(del_line)));
}

TEST(Parser, VerbIsCaseInsensitive) {
    const InputLine line{"get key"};
    EXPECT_TRUE(parse(line).has_value());
}

TEST(Parser, IncrDefaultsToOne) {
    const InputLine line{"INCR counter"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    ASSERT_TRUE(std::holds_alternative<IncrCommand>(*command));
    EXPECT_EQ(std::get<IncrCommand>(*command).by, 1);
}

TEST(Parser, IncrRejectsTrailingGarbage) {
    const InputLine line{"INCR counter 12abc"};
    EXPECT_FALSE(parse(line).has_value());
}

TEST(Parser, RejectsWrongArity) {
    const InputLine too_few{"SET key"};
    const InputLine too_many{"GET a b"};
    EXPECT_FALSE(parse(too_few).has_value());
    EXPECT_FALSE(parse(too_many).has_value());
}

TEST(Parser, RejectsUnknownVerb) {
    const InputLine line{"FLUSHALL"};
    EXPECT_FALSE(parse(line).has_value());
}

TEST(Parser, ResultBorrowsFromInputLine) {
    const InputLine line{"SET key value"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    const auto& [key, value] = std::get<SetCommand>(*command);
    EXPECT_EQ(key.data(), line.view().data() + 4);
}