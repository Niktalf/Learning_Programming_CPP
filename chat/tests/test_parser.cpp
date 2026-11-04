#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "chat/command.hpp"
#include "chat/parser.hpp"

using chat::Command;
using chat::InputLine;
using chat::parse;

namespace {

// Разобранная команда смотрит внутрь строки, поэтому InputLine заводится в
// тесте как переменная и живёт до конца проверки.
//
// Альтернатива возвращается по значению, а не по ссылке: аргумент — временный
// optional, и ссылка в него повисла бы в конце полного выражения. Компилятор
// с -Wdangling-reference ловит именно эту ошибку — ту самую, которой посвящён
// этап. Сами команды — пары string_view, копировать их дёшево.
template <class T>
T as(const std::optional<Command>& command) {
    if (!command || !std::holds_alternative<T>(*command)) {
        ADD_FAILURE() << "unexpected command alternative";
        return T{};
    }
    return std::get<T>(*command);
}

}  // namespace

TEST(Parser, ParsesJoinAndLeave) {
    const InputLine join{"/join #general"};
    EXPECT_EQ(as<chat::JoinCommand>(parse(join)).room, std::string_view("#general"));

    const InputLine leave{"/LEAVE #general"};   // регистр команды не важен
    EXPECT_EQ(as<chat::LeaveCommand>(parse(leave)).room, std::string_view("#general"));
}

TEST(Parser, MsgTakesRestOfLineAsText) {
    const InputLine line{"/msg #general  всем привет,   как дела"};
    const auto msg = as<chat::MsgCommand>(parse(line));
    EXPECT_EQ(msg.room, std::string_view("#general"));
    EXPECT_EQ(msg.text, std::string_view("всем привет,   как дела"));
}

TEST(Parser, MsgTextMayContainQuotes) {
    // Кавычки в тексте сообщения — просто символы: хвост лексер не разбирает.
    const InputLine line{R"(/msg #general он сказал "привет)"};
    EXPECT_EQ(as<chat::MsgCommand>(parse(line)).text, std::string_view(R"(он сказал "привет)"));
}

TEST(Parser, TopicWithQuotesIsThreeTokens) {
    const InputLine line{R"(/topic #general "обсуждаем курс")"};
    const auto topic = as<chat::TopicCommand>(parse(line));
    ASSERT_TRUE(topic.topic.has_value());
    EXPECT_EQ(*topic.topic, std::string_view("обсуждаем курс"));
}

TEST(Parser, TopicWithoutTextMeansShow) {
    const InputLine line{"/topic #general"};
    EXPECT_FALSE(as<chat::TopicCommand>(parse(line)).topic.has_value());
}

TEST(Parser, UnquotedMultiWordTopicIsRejected) {
    const InputLine line{"/topic #general обсуждаем курс"};
    EXPECT_FALSE(parse(line).has_value());
}

TEST(Parser, HistoryHasDefaultLimit) {
    const InputLine line{"/history #general"};
    EXPECT_EQ(as<chat::HistoryCommand>(parse(line)).limit, 10u);

    const InputLine explicit_limit{"/history #general 3"};
    EXPECT_EQ(as<chat::HistoryCommand>(parse(explicit_limit)).limit, 3u);
}

TEST(Parser, ReactDefaultsToOne) {
    const InputLine line{"/react #general 2"};
    const auto react = as<chat::ReactCommand>(parse(line));
    EXPECT_EQ(react.index, 2u);
    EXPECT_EQ(react.by, 1);
}

TEST(Parser, ParsesWhoAndType) {
    const InputLine who{"/who #general"};
    EXPECT_TRUE(std::holds_alternative<chat::WhoCommand>(*parse(who)));

    const InputLine type{"/type #general 0"};
    EXPECT_EQ(as<chat::TypeCommand>(parse(type)).index, 0u);
}

TEST(Parser, RejectsMalformedInput) {
    for (const char* const text :
         {"", "   ", "/msg", "/msg #general", "/join general", "/join #", "/history #general x",
          "/history #general 3x", "/react #general", "/type #general -1", "/unknown #general",
          R"(/topic #general "незакрыто)", "hello"}) {
        const InputLine line{text};
        EXPECT_FALSE(parse(line).has_value()) << text;
    }
}

TEST(Parser, ResultBorrowsFromInputLine) {
    const InputLine line{"/join #general"};
    const auto join = as<chat::JoinCommand>(parse(line));
    // Команда не копирует символы: имя комнаты указывает внутрь строки.
    EXPECT_EQ(join.room.data(), line.view().data() + 6);
}
