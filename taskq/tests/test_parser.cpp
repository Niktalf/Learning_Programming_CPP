#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include "taskq/command.hpp"
#include "taskq/parser.hpp"

using taskq::Command;
using taskq::InputLine;
using taskq::parse;

namespace {

// Альтернатива возвращается по значению, а не по ссылке: аргумент — временная
// команда, и ссылка в неё повисла бы в конце полного выражения. Компилятор
// с -Wdangling-reference ловит именно эту ошибку — ту самую, которой посвящён
// этап 2. Команды — string_view и числа, копировать их дёшево.
template <class T>
T as(const Command& command) {
    if (!std::holds_alternative<T>(command)) {
        ADD_FAILURE() << "unexpected command alternative";
        return T{};
    }
    return std::get<T>(command);
}

// Причина отказа разбора или пустая строка, если разбор прошёл.
std::string reason_for(const char* text) {
    const InputLine line{text};
    try {
        static_cast<void>(parse(line));
    } catch (const taskq::ParseError& e) {
        return e.reason();
    }
    return {};
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
        EXPECT_THROW(static_cast<void>(parse(line)), taskq::ParseError) << text;
    }
    EXPECT_EQ(reason_for("SUBMIT build prio=10 x"), std::string("priority must be 0..9: 10"));
    EXPECT_EQ(reason_for("SUBMIT build prio=5"), std::string("SUBMIT needs a body"));
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

    const InputLine release{"RELEASE 11"};
    EXPECT_EQ(as<taskq::ReleaseCommand>(parse(release)).id, 11u);
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
          "BUMP 1 x", R"(SUBMIT "build 5 make)", "UNKNOWN 1", "hello", "CREATE",
          "SUBMITBATCH", "SUBMITBATCH build", "SUBMITBATCH build prio=3"}) {
        const InputLine line{text};
        EXPECT_THROW(static_cast<void>(parse(line)), taskq::ParseError) << text;
    }
}

TEST(Parser, ErrorCarriesReasonAndInput) {
    // Ради этого этап 3 и затевался: пустой optional говорил «не разобрано»,
    // но не говорил почему.
    EXPECT_EQ(reason_for("FROB 1"), std::string("unknown command: FROB"));
    EXPECT_EQ(reason_for("ACK abc"), std::string("task id must be a non-negative number: abc"));
    EXPECT_EQ(reason_for("ACK 1 2"), std::string("ACK expects a task id"));
    EXPECT_EQ(reason_for(R"(SUBMIT "   " x)"), std::string("queue name is empty"));

    const InputLine line{"ACK abc"};
    try {
        static_cast<void>(parse(line));
        ADD_FAILURE() << "parse must throw";
    } catch (const taskq::ParseError& e) {
        EXPECT_EQ(e.input(), std::string("ACK abc"));
    }
}

TEST(Parser, BatchOfItemsWithOptionalPriority) {
    const InputLine line{R"(SUBMITBATCH build "make all" deploy prio=7 "ship it" build num:3)"};
    const auto batch = as<taskq::BatchCommand>(parse(line));
    ASSERT_EQ(batch.items.size(), 3u);
    EXPECT_EQ(batch.items[0].queue, std::string_view("build"));
    EXPECT_EQ(batch.items[0].body, std::string_view("make all"));
    EXPECT_EQ(batch.items[1].priority, 7);
    EXPECT_EQ(batch.items[1].body, std::string_view("ship it"));
    EXPECT_EQ(batch.items[2].body, std::string_view("num:3"));

    EXPECT_EQ(reason_for("SUBMITBATCH build x deploy"),
              std::string("SUBMITBATCH item for deploy has no body"));
}

TEST(Parser, CreateAndPollTakeQueue) {
    const InputLine create{R"(CREATE "build tasks")"};
    EXPECT_EQ(as<taskq::CreateCommand>(parse(create)).queue, std::string_view("build tasks"));
    EXPECT_EQ(reason_for(R"(CREATE " build")"),
              std::string("queue name starts or ends with a space"));
}

TEST(Parser, ResultBorrowsFromInputLine) {
    const InputLine line{"POLL build"};
    const auto poll = as<taskq::PollCommand>(parse(line));
    // Команда не копирует символы: имя очереди указывает внутрь строки.
    EXPECT_EQ(poll.queue.data(), line.view().data() + 5);
}
