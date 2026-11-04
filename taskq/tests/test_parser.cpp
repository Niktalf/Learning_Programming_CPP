#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "taskq/command.hpp"
#include "taskq/parser.hpp"

using taskq::Command;
using taskq::InputLine;
using taskq::parse;

namespace {

// Альтернатива возвращается по значению, а не по ссылке: аргумент — временный
// optional, и ссылка в него повисла бы в конце полного выражения. Компилятор
// с -Wdangling-reference ловит именно эту ошибку — ту самую, которой посвящён
// этап. Команды — пары string_view и числа, копировать их дёшево.
template <class T>
T as(const std::optional<Command>& command) {
    if (!command || !std::holds_alternative<T>(*command)) {
        ADD_FAILURE() << "unexpected command alternative";
        return T{};
    }
    return std::get<T>(*command);
}

}  // namespace

TEST(Parser, SubmitWithQuotedQueueAndPriority) {
    const InputLine line{R"(SUBMIT "build tasks" prio=5 make all)"};
    const auto submit = as<taskq::SubmitCommand>(parse(line));
    EXPECT_EQ(submit.queue, std::string_view("build tasks"));
    EXPECT_EQ(submit.priority, 5);
    EXPECT_EQ(submit.body, std::string_view("make all"));
}

TEST(Parser, SubmitPriorityIsOptional) {
    const InputLine line{"submit build make   -j8 all"};   // регистр не важен
    const auto submit = as<taskq::SubmitCommand>(parse(line));
    EXPECT_EQ(submit.queue, std::string_view("build"));
    EXPECT_EQ(submit.priority, 0);
    EXPECT_EQ(submit.body, std::string_view("make   -j8 all"));
}

TEST(Parser, SubmitBodyMayContainQuotes) {
    // Кавычки в теле задачи — просто символы: хвост лексер не разбирает.
    const InputLine line{R"(SUBMIT build echo "hi)"};
    EXPECT_EQ(as<taskq::SubmitCommand>(parse(line)).body, std::string_view(R"(echo "hi)"));
}

TEST(Parser, PriorityOutOfRangeOrGarbageIsRejected) {
    for (const char* const text : {"SUBMIT build prio=10 x", "SUBMIT build prio=-1 x",
                                   "SUBMIT build prio=5x x", "SUBMIT build prio=5"}) {
        const InputLine line{text};
        EXPECT_FALSE(parse(line).has_value()) << text;
    }
}

TEST(Parser, TaskCommandsTakeNumericId) {
    const InputLine ack{"ACK 7"};
    EXPECT_EQ(as<taskq::AckCommand>(parse(ack)).id, 7u);

    const InputLine fail{"FAIL 8"};
    EXPECT_EQ(as<taskq::FailCommand>(parse(fail)).id, 8u);

    const InputLine status{"STATUS 9"};
    EXPECT_EQ(as<taskq::StatusCommand>(parse(status)).id, 9u);

    const InputLine type{"TYPE 10"};
    EXPECT_EQ(as<taskq::TypeCommand>(parse(type)).id, 10u);
}

TEST(Parser, BumpDefaultsToOne) {
    const InputLine line{"BUMP 3"};
    const auto bump = as<taskq::BumpCommand>(parse(line));
    EXPECT_EQ(bump.id, 3u);
    EXPECT_EQ(bump.by, 1);

    const InputLine explicit_step{"BUMP 3 -2"};
    EXPECT_EQ(as<taskq::BumpCommand>(parse(explicit_step)).by, -2);
}

TEST(Parser, RejectsMalformedInput) {
    for (const char* const text :
         {"", "   ", "SUBMIT", "SUBMIT build", "POLL", "ACK", "ACK abc", "ACK 1 2", "ACK -1",
          "BUMP 1 x", R"(SUBMIT "build 5 make)", "UNKNOWN 1", "hello"}) {
        const InputLine line{text};
        EXPECT_FALSE(parse(line).has_value()) << text;
    }
}

TEST(Parser, ResultBorrowsFromInputLine) {
    const InputLine line{"POLL build"};
    const auto poll = as<taskq::PollCommand>(parse(line));
    // Команда не копирует символы: имя очереди указывает внутрь строки.
    EXPECT_EQ(poll.queue.data(), line.view().data() + 5);
}
