#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include "parser.hpp"

TEST(Parser, ParsesSubmitWithTailBody) {
    const InputLine line{"SUBMIT build 5 make -j8 all"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    ASSERT_TRUE(std::holds_alternative<SubmitCommand>(*command));
    const auto& submit = std::get<SubmitCommand>(*command);
    EXPECT_EQ(submit.queue, std::string_view("build"));
    EXPECT_EQ(submit.priority, 5);
    EXPECT_EQ(submit.body, std::string_view("make -j8 all"));
}

TEST(Parser, ParsesPoll) {
    const InputLine line{"POLL build"};
    EXPECT_TRUE(std::holds_alternative<PollCommand>(*parse(line)));
}

TEST(Parser, VerbIsCaseInsensitive) {
    const InputLine line{"poll build"};
    EXPECT_TRUE(parse(line).has_value());
}

TEST(Parser, PriorityOutOfRangeIsRejected) {
    const InputLine too_big{"SUBMIT build 42 make all"};
    const InputLine negative{"SUBMIT build -1 make all"};
    EXPECT_FALSE(parse(too_big).has_value());
    EXPECT_FALSE(parse(negative).has_value());
}

TEST(Parser, PriorityRejectsTrailingGarbage) {
    const InputLine line{"SUBMIT build 5x make all"};
    EXPECT_FALSE(parse(line).has_value());
}

TEST(Parser, EmptyBodyIsRejected) {
    const InputLine line{"SUBMIT build 5    "};
    EXPECT_FALSE(parse(line).has_value());
}

TEST(Parser, BumpDefaultsToOne) {
    const InputLine line{"BUMP build"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    EXPECT_EQ(std::get<BumpCommand>(*command).by, 1);
}

TEST(Parser, RejectsUnknownVerb) {
    const InputLine line{"FLUSH build"};
    EXPECT_FALSE(parse(line).has_value());
}

TEST(Parser, ResultBorrowsFromInputLine) {
    const InputLine line{"POLL build"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    EXPECT_EQ(std::get<PollCommand>(*command).queue.data(), line.view().data() + 5);
}
