// Реестр команд этапа 6 сам по себе, без хранилища: контекст — игрушечная
// структура. Так видно, что реестр знает только сигнатуры обработчиков и
// ничего не знает о командах чата.

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "chat/errors.hpp"
#include "chat/registry.hpp"

using chat::CommandRegistry;
using chat::Response;
using chat::RoomName;
using chat::Tail;

namespace {

struct Board {
    std::vector<std::string> lines;
};

Response post(Board& board, RoomName room, Tail text) {
    board.lines.push_back(std::string(room.name) + ": " + std::string(text.text));
    return Response{true, std::to_string(board.lines.size())};
}

Response count(Board& board, std::optional<std::size_t> last) {
    return Response{true, std::to_string(last.value_or(board.lines.size()))};
}

const CommandRegistry<Board>& registry() {
    static const CommandRegistry<Board> table = [] {
        CommandRegistry<Board> result;
        result.register_command("/post", post)
            .register_command("/count", count)
            // Лямбда без захвата тоже подходит: сигнатуру function_traits
            // берёт из operator().
            .register_command("/clear", [](Board& board) {
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
            std::rethrow_exception(chat::root_cause(std::current_exception()));
        } catch (const chat::ParseError& e) {
            return e.reason();
        } catch (...) {
            return "(other error)";
        }
    }
    return "(no error)";
}

}  // namespace

TEST(Registry, TailTakesTheRestOfTheLineVerbatim) {
    Board board;
    EXPECT_EQ(registry().bind(R"(/post #a  hello,   "world)")(board).text, std::string("1"));
    ASSERT_EQ(board.lines.size(), 1u);
    EXPECT_EQ(board.lines.front(), std::string(R"(#a: hello,   "world)"));
}

TEST(Registry, OptionalArgument) {
    Board board;
    static_cast<void>(registry().bind("/post #a x")(board));
    EXPECT_EQ(registry().bind("/count")(board).text, std::string("1"));
    EXPECT_EQ(registry().bind("/count 7")(board).text, std::string("7"));
}

TEST(Registry, UsageComesFromTheSignature) {
    EXPECT_EQ(failure("/post #a"), std::string("usage: /post <#room> <text...>"));
    EXPECT_EQ(failure("/count 1 2"), std::string("usage: /count [number]"));
    EXPECT_EQ(failure("/clear now"), std::string("usage: /clear"));
}

TEST(Registry, TypedArgumentsAreValidated) {
    EXPECT_EQ(failure("/post general x"), std::string("room name must start with #: general"));
    EXPECT_EQ(failure("/post #a \x1b[2J"), std::string("text contains control characters"));
    EXPECT_EQ(failure("/count -3"), std::string("not a non-negative number: -3"));
}

TEST(Registry, LineChecksComeFirst) {
    EXPECT_EQ(failure("/post #a " + std::string(5000, 'x')),
              std::string("line too long: 5009 bytes, limit is 4096"));
    EXPECT_EQ(failure("/post #a \xd0"), std::string("invalid UTF-8"));
    EXPECT_EQ(failure(""), std::string("empty command"));
    EXPECT_EQ(failure("/frob"), std::string("unknown command: /frob"));
}

TEST(Registry, ConversionHappensAtBindNotAtCall) {
    // Разбор и исполнение разделены: ошибка аргумента — при bind, до того
    // как обработчик тронул контекст.
    EXPECT_ANY_THROW(static_cast<void>(registry().bind("/count nope")));
}

TEST(Registry, ArgumentsBorrowFromTheLine) {
    CommandRegistry<std::string_view> echo;
    echo.register_command("/echo", [](std::string_view& out, Tail text) {
        out = text.text;
        return Response{true, "OK"};
    });
    const std::string line = "/echo payload here";
    std::string_view captured;
    static_cast<void>(echo.bind(line)(captured));
    EXPECT_EQ(captured.data(), line.data() + 6);
}

TEST(Registry, DuplicateNameIsALogicError) {
    // Повтор имени обычного реестра виден только при запуске. Звёздочка —
    // StaticRegistry — ловит его при компиляции.
    CommandRegistry<Board> table;
    table.register_command("/post", post);
    EXPECT_THROW(table.register_command("/POST", post), std::logic_error);
}

TEST(Registry, ErrorInHandlerGetsContext) {
    CommandRegistry<Board> table;
    table.register_command("/fail", [](Board&, RoomName room) -> Response {
        throw chat::UnknownRoomError(std::string(room.name));
    });
    Board board;
    try {
        static_cast<void>(table.bind("/fail #nowhere")(board));
        ADD_FAILURE() << "handler must throw";
    } catch (...) {
        const auto context = chat::context_of(std::current_exception());
        ASSERT_EQ(context.size(), 1u);
        EXPECT_EQ(context.front(), std::string("while executing /fail #nowhere"));
    }
}

TEST(Registry, NamesAreSorted) {
    const std::vector<std::string> expected = {"/clear", "/count", "/post"};
    EXPECT_EQ(registry().names(), expected);
}

// Концепция CommandHandler — при компиляции. Негативный случай с полной
// диагностикой — tests/compile_fail/bad_handler.cpp.
static_assert(chat::CommandHandler<decltype(&post), Board>);
static_assert(!chat::CommandHandler<decltype(&post), int>);                      // чужой контекст
static_assert(!chat::CommandHandler<Response (*)(Board&, bool), Board>);         // bool не разбирается
static_assert(!chat::CommandHandler<Response (*)(Board&, Tail, RoomName), Board>);   // Tail не последним
static_assert(!chat::CommandHandler<
              Response (*)(Board&, std::optional<RoomName>, Tail), Board>);      // Tail после необязательного
static_assert(!chat::CommandHandler<Response (*)(Board&, std::optional<Tail>), Board>);   // Tail не бывает необязательным
static_assert(!chat::CommandHandler<int (*)(Board&), Board>);                    // результат не Response
