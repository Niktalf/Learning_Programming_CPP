#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include "errors.hpp"
#include "parser.hpp"

TEST(Parser, ParsesSubmitWithTailBody) {
    const InputLine line{"SUBMIT build 5 make -j8 all"};
    const auto command = parse(line);

    ASSERT_TRUE(std::holds_alternative<SubmitCommand>(command));

    const auto& [queue, priority, body] = std::get<SubmitCommand>(command);

    EXPECT_EQ(queue, std::string_view("build"));
    EXPECT_EQ(priority, 5);
    EXPECT_EQ(body, std::string_view("make -j8 all"));
}

TEST(Parser, AckTakesTaskId) {
    const InputLine line{"ACK 42"};
    const auto command = parse(line);

    ASSERT_TRUE(std::holds_alternative<AckCommand>(command));
    EXPECT_EQ(std::get<AckCommand>(command).id, 42u);
}

TEST(Parser, VerbIsCaseInsensitive) {
    const InputLine line{"poll build w1"};

    EXPECT_TRUE(std::holds_alternative<PollCommand>(parse(line)));
}

TEST(Parser, PriorityOutOfRangeIsRejected) {
    const InputLine too_big{"SUBMIT build 42 make all"};
    const InputLine negative{"SUBMIT build -1 make all"};

    EXPECT_THROW((void)parse(too_big), ParseError);
    EXPECT_THROW((void)parse(negative), ParseError);
}

TEST(Parser, PriorityRejectsTrailingGarbage) {
    const InputLine line{"SUBMIT build 5x make all"};

    EXPECT_THROW((void)parse(line), ParseError);
}

TEST(Parser, TaskIdRejectsGarbage) {
    const InputLine line{"ACK 12abc"};

    EXPECT_THROW((void)parse(line), ParseError);
}

TEST(Parser, EmptyBodyIsRejected) {
    const InputLine line{"SUBMIT build 5    "};

    EXPECT_THROW((void)parse(line), ParseError);
}

TEST(Parser, RejectsUnknownVerb) {
    const InputLine line{"FLUSH build"};

    EXPECT_THROW((void)parse(line), ParseError);
}

TEST(Parser, ResultBorrowsFromInputLine) {
    const InputLine line{"POLL build w1"};
    const auto command = parse(line);

    EXPECT_EQ(std::get<PollCommand>(command).queue.data(), line.view().data() + 5);
    EXPECT_EQ(std::get<PollCommand>(command).worker, std::string_view("w1"));
}
