// Реестр команд этапа 6 сам по себе, без сервиса: контекст — игрушечная
// структура. Так видно, что реестр знает только сигнатуры обработчиков и
// ничего не знает о командах очереди.

#include <gtest/gtest.h>

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/registry.hpp"

using taskq::CommandRegistry;
using taskq::Limit;
using taskq::Priority;
using taskq::QueueName;
using taskq::Response;
using taskq::Tail;

namespace {

struct Board {
    std::vector<std::string> lines;

    // Метод тоже обработчик: объект — контекст.
    Response count() const { return Response{true, std::to_string(lines.size())}; }
};

Response post(Board& board, QueueName queue, std::optional<Priority> priority, Tail text) {
    board.lines.push_back(std::string(queue.name) + "/" + std::to_string(priority ? priority->value : 0) +
                          ": " + std::string(text.text));
    return Response{true, board.lines.back()};
}

Response show(const Board& board, std::optional<Limit> limit, std::optional<Priority> priority) {
    return Response{true, std::to_string(limit ? limit->value : board.lines.size()) + "/" +
                              std::to_string(priority ? priority->value : -1)};
}

const CommandRegistry<Board>& registry() {
    static const CommandRegistry<Board> table = [] {
        CommandRegistry<Board> result;
        result.register_command("POST", post)
            .register_command("SHOW", show)
            .register_command("COUNT", &Board::count)
            // Лямбда без захвата тоже подходит.
            .register_command("CLEAR", [](Board& board) {
                board.lines.clear();
                return Response{true, "OK"};
            });
        return result;
    }();
    return table;
}

// Причина ParseError по корню цепочки или «(no error)».
std::string failure(std::string_view line) {
    try {
        static_cast<void>(registry().bind(line));
    } catch (...) {
        try {
            std::rethrow_exception(taskq::root_cause(std::current_exception()));
        } catch (const taskq::ParseError& e) {
            return e.reason();
        } catch (...) {
            return "(other error)";
        }
    }
    return "(no error)";
}

}  // namespace

TEST(Registry, NamedOptionalBeforeTail) {
    // То, ради чего у SUBMIT строка разбиралась дважды: prio=N перед телом
    // есть или нет — видно по префиксу следующего токена.
    Board board;
    EXPECT_EQ(registry().bind(R"(POST build prio=3 echo "hi)")(board).text, std::string(R"(build/3: echo "hi)"));
    EXPECT_EQ(registry().bind(R"(POST build echo "hi)")(board).text, std::string(R"(build/0: echo "hi)"));
    // Тело, похожее на приоритет, но не первым словом, — просто тело.
    EXPECT_EQ(registry().bind("POST build x prio=3")(board).text, std::string("build/0: x prio=3"));
}

TEST(Registry, NamedArgumentsInAnyOrder) {
    Board board;
    EXPECT_EQ(registry().bind("SHOW limit=5 prio=2")(board).text, std::string("5/2"));
    EXPECT_EQ(registry().bind("SHOW prio=2 limit=5")(board).text, std::string("5/2"));
    EXPECT_EQ(registry().bind("SHOW")(board).text, std::string("0/-1"));
    EXPECT_EQ(failure("SHOW prio=1 prio=2"), std::string("unexpected argument: prio=2"));
    EXPECT_EQ(failure("SHOW 5"), std::string("unexpected argument: 5"));
}

TEST(Registry, MissingArgumentsAreNamed) {
    EXPECT_EQ(failure("POST"), std::string("missing <queue>"));
    EXPECT_EQ(failure("POST build prio=1"), std::string("missing <body...>"));
}

TEST(Registry, UsageComesFromTheSignature) {
    try {
        static_cast<void>(registry().bind("POST"));
        ADD_FAILURE() << "must throw";
    } catch (...) {
        const auto context = taskq::context_of(std::current_exception());
        ASSERT_EQ(context.size(), 1u);
        EXPECT_EQ(context.front(), std::string("usage: POST <queue> [prio=0..9] <body...>"));
    }
}

TEST(Registry, LineChecksComeFirst) {
    EXPECT_EQ(failure(""), std::string("empty command"));
    EXPECT_EQ(failure("FROB"), std::string("unknown command: FROB"));
    EXPECT_EQ(failure("POST q \xd0"), std::string("invalid UTF-8 (use bin: for binary payloads)"));
}

TEST(Registry, MemberFunctionHandler) {
    Board board;
    static_cast<void>(registry().bind("POST q a")(board));
    EXPECT_EQ(registry().bind("count")(board).text, std::string("1"));
    EXPECT_TRUE(registry().bind("COUNT").reads_only());   // константный метод
    EXPECT_TRUE(registry().bind("SHOW").reads_only());    // const Board&
    EXPECT_FALSE(registry().bind("CLEAR").reads_only());
}

TEST(Registry, ArgumentsBorrowFromTheLine) {
    CommandRegistry<std::string_view> echo;
    echo.register_command("ECHO", [](std::string_view& out, Tail text) {
        out = text.text;
        return Response{true, "OK"};
    });
    const std::string line = "ECHO payload here";
    std::string_view captured;
    static_cast<void>(echo.bind(line)(captured));
    EXPECT_EQ(captured.data(), line.data() + 5);
}

TEST(Registry, DuplicateNameIsALogicError) {
    // Повтор имени обычного реестра виден только при запуске. Звёздочка —
    // StaticRegistry — ловит его при компиляции.
    CommandRegistry<Board> table;
    table.register_command("POST", post);
    EXPECT_THROW(table.register_command("post", post), std::logic_error);
}

TEST(Registry, ErrorInHandlerGetsContext) {
    CommandRegistry<Board> table;
    table.register_command("FAIL", [](Board&, QueueName queue) -> Response {
        throw taskq::UnknownQueueError(std::string(queue.name));
    });
    Board board;
    try {
        static_cast<void>(table.bind("FAIL nowhere")(board));
        ADD_FAILURE() << "handler must throw";
    } catch (...) {
        const auto context = taskq::context_of(std::current_exception());
        ASSERT_EQ(context.size(), 1u);
        EXPECT_EQ(context.front(), std::string("while executing FAIL nowhere"));
    }
}

TEST(Registry, NamesAreSorted) {
    const std::vector<std::string> expected = {"CLEAR", "COUNT", "POST", "SHOW"};
    EXPECT_EQ(registry().names(), expected);
}

// Концепция CommandHandler — при компиляции. Негативный случай с полной
// диагностикой — tests/compile_fail/bad_handler.cpp.
static_assert(taskq::CommandHandler<decltype(&post), Board>);
static_assert(taskq::CommandHandler<decltype(&Board::count), Board>);
static_assert(!taskq::CommandHandler<decltype(&post), int>);                                // чужой контекст
static_assert(!taskq::CommandHandler<Response (*)(Board&, bool), Board>);                   // bool не разбирается
static_assert(!taskq::CommandHandler<Response (*)(Board&, Tail, QueueName), Board>);        // Tail не последним
static_assert(!taskq::CommandHandler<
              Response (*)(Board&, std::optional<QueueName>, Tail), Board>);                // Tail после необязательного
static_assert(taskq::CommandHandler<
              Response (*)(Board&, std::optional<Priority>, Tail), Board>);                 // а после именованного — можно
static_assert(!taskq::CommandHandler<
              Response (*)(Board&, std::optional<Limit>, QueueName), Board>);               // обязательный после именованного
static_assert(!taskq::CommandHandler<int (*)(Board&), Board>);                              // результат не Response
