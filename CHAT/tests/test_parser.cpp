#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include "parser.hpp"

TEST(Parser, ParsesJoin) {
    const InputLine line{"/join #general"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    ASSERT_TRUE(std::holds_alternative<JoinCommand>(*command));
    EXPECT_EQ(std::get<JoinCommand>(*command).room, std::string_view("#general"));
}

TEST(Parser, MsgTakesRestOfLineAsText) {
    const InputLine line{"/msg #general hi everyone, how are you"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    ASSERT_TRUE(std::holds_alternative<MsgCommand>(*command));
    EXPECT_EQ(std::get<MsgCommand>(*command).text, std::string_view("hi everyone, how are you"));
}

TEST(Parser, RoomMustStartWithHash) {
    const InputLine line{"/join general"};
    EXPECT_FALSE(parse(line).has_value());
}

TEST(Parser, EmptyMessageTextIsRejected) {
    const InputLine line{"/msg #general    "};
    EXPECT_FALSE(parse(line).has_value());
}

TEST(Parser, HistoryHasDefaultLimit) {
    const InputLine line{"/history #general"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    EXPECT_EQ(std::get<HistoryCommand>(*command).limit, 20u);
}

TEST(Parser, HistoryRejectsTrailingGarbage) {
    const InputLine line{"/history #general 12abc"};
    EXPECT_FALSE(parse(line).has_value());
}

TEST(Parser, ReactDefaultsToOne) {
    const InputLine line{"/react #general 0"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    EXPECT_EQ(std::get<ReactCommand>(*command).by, 1);
}

TEST(Parser, VerbIsCaseInsensitive) {
    const InputLine line{"/JOIN #general"};
    EXPECT_TRUE(parse(line).has_value());
}

TEST(Parser, ResultBorrowsFromInputLine) {
    const InputLine line{"/join #general"};
    const auto command = parse(line);

    ASSERT_TRUE(command.has_value());
    EXPECT_EQ(std::get<JoinCommand>(*command).room.data(), line.view().data() + 6);
}
