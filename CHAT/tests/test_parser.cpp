#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include "errors.hpp"
#include "parser.hpp"


TEST(Parser, ParsesJoin) {
    const InputLine line{"/join #general"};
    const auto command = parse(line);

    ASSERT_TRUE(std::holds_alternative<JoinCommand>(command));
    EXPECT_EQ(std::get<JoinCommand>(command).room, std::string_view("#general"));
}

TEST(Parser, MsgTakesRestOfLineAsText) {
    const InputLine line{"/msg #general hi everyone, how are you"};
    const auto command = parse(line);

    ASSERT_TRUE(std::holds_alternative<MsgCommand>(command));
    const auto& msg = std::get<MsgCommand>(command);
    ASSERT_EQ(msg.rooms.size(), 1u);
    EXPECT_EQ(msg.text, std::string_view("hi everyone, how are you"));
}

TEST(Parser, RoomMustStartWithHash) {
    const InputLine line{"/join general"};
    EXPECT_THROW((void)parse(line), ParseError);
}

TEST(Parser, EmptyMessageTextIsRejected) {
    const InputLine line{"/msg #general    "};
    EXPECT_THROW((void)parse(line), ParseError);
}

TEST(Parser, HistoryHasDefaultLimit) {
    const InputLine line{"/history #general"};
    const auto command = parse(line);

    EXPECT_EQ(std::get<HistoryCommand>(command).limit, 20u);
}

TEST(Parser, HistoryRejectsTrailingGarbage) {
    const InputLine line{"/history #general 12abc"};
    EXPECT_THROW((void)parse(line), ParseError);
}

TEST(Parser, ReactDefaultsToOne) {
    const InputLine line{"/react #general 0"};
    const auto command = parse(line);

    EXPECT_EQ(std::get<ReactCommand>(command).by, 1);
}

TEST(Parser, VerbIsCaseInsensitive) {
    const InputLine line{"/JOIN #general"};
    EXPECT_NO_THROW((void)parse(line));
}

TEST(Parser, ResultBorrowsFromInputLine) {
    const InputLine line{"/join #general"};
    const auto command = parse(line);

    EXPECT_EQ(std::get<JoinCommand>(command).room.data(), line.view().data() + 6);
}

TEST(Parser, MsgToSeveralRooms) {
    const InputLine line{"/msg #a #b #c текст"};
    const auto command = parse(line);
    const auto& msg = std::get<MsgCommand>(command);

    ASSERT_EQ(msg.rooms.size(), 3u);
    EXPECT_EQ(msg.rooms[2], std::string_view("#c"));
    EXPECT_EQ(msg.text, std::string_view("текст"));
}

TEST(Parser, MsgWithoutRoomIsRejected) {
    const InputLine line{"/msg просто текст"};

    EXPECT_THROW((void)parse(line), ParseError);
}
