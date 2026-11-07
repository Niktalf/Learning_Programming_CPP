// Реестр команд этапа 6 сам по себе, без хранилища: контекст — игрушечная
// структура. Так видно, что реестр знает только сигнатуры обработчиков и
// ничего не знает о командах KVS.

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "kvs/errors.hpp"
#include "kvs/lexer.hpp"
#include "kvs/registry.hpp"

using kvs::CommandRegistry;
using kvs::Effect;
using kvs::Response;
using kvs::Rest;

namespace {

struct Counter {
    std::int64_t total = 0;
    std::vector<std::string> seen;
};

Response add(Counter& counter, std::int64_t by) {
    counter.total += by;
    return Response{true, std::to_string(counter.total)};
}

Response greet(Counter&, std::string_view name, std::optional<std::string_view> greeting) {
    return Response{true, std::string(greeting.value_or("hello")) + ", " + std::string(name)};
}

Response collect(Counter& counter, std::string_view first, Rest rest) {
    counter.seen.emplace_back(first);
    for (const std::string_view token : rest.tokens) {
        counter.seen.emplace_back(token);
    }
    return Response{true, std::to_string(counter.seen.size())};
}

const CommandRegistry<Counter>& registry() {
    static const CommandRegistry<Counter> table = [] {
        CommandRegistry<Counter> result;
        result.add("ADD", Effect::Writes, add)
            .add("GREET", Effect::Reads, greet)
            .add("COLLECT", Effect::Writes, collect)
            // Лямбда без захвата тоже подходит: её сигнатуру function_traits
            // берёт из operator().
            .add("RESET", Effect::Writes, [](Counter& counter) {
                counter.total = 0;
                return Response{true, "OK"};
            });
        return result;
    }();
    return table;
}

Response run(Counter& counter, std::string_view line) {
    const kvs::Tokens tokens = kvs::tokenize(line);
    return registry().bind(tokens, line)(counter);
}

// Причина ParseError, которую бросает разбор строки.
std::string parse_failure(std::string_view line) {
    try {
        const kvs::Tokens tokens = kvs::tokenize(line);
        static_cast<void>(registry().bind(tokens, line));
    } catch (const kvs::ParseError& e) {
        return e.reason();
    }
    return "(no error)";
}

}  // namespace

TEST(Registry, ConvertsArgumentsBySignature) {
    Counter counter;
    EXPECT_EQ(run(counter, "ADD 40").text, std::string("40"));
    EXPECT_EQ(run(counter, "ADD 2").text, std::string("42"));
    EXPECT_EQ(run(counter, "RESET").text, std::string("OK"));
    EXPECT_EQ(counter.total, 0);
}

TEST(Registry, VerbIsCaseInsensitive) {
    Counter counter;
    EXPECT_EQ(run(counter, "add 1").text, std::string("1"));
    EXPECT_EQ(run(counter, "Add 1").text, std::string("2"));
}

TEST(Registry, OptionalArgument) {
    Counter counter;
    EXPECT_EQ(run(counter, "GREET world").text, std::string("hello, world"));
    EXPECT_EQ(run(counter, "GREET world hi").text, std::string("hi, world"));
}

TEST(Registry, RestTakesEverythingLeft) {
    Counter counter;
    EXPECT_EQ(run(counter, "COLLECT a b c d").text, std::string("4"));
    EXPECT_EQ(run(counter, "COLLECT e").text, std::string("5"));
}

TEST(Registry, ArityComesFromTheSignature) {
    // Ни один обработчик арность не проверяет: реестр выводит её сам.
    EXPECT_EQ(parse_failure("ADD"), std::string("ADD expects 1 argument"));
    EXPECT_EQ(parse_failure("ADD 1 2"), std::string("ADD expects 1 argument"));
    EXPECT_EQ(parse_failure("GREET"), std::string("GREET expects 1 to 2 arguments"));
    EXPECT_EQ(parse_failure("GREET a b c"), std::string("GREET expects 1 to 2 arguments"));
    EXPECT_EQ(parse_failure("COLLECT"), std::string("COLLECT expects at least 1 argument"));
    EXPECT_EQ(parse_failure("RESET now"), std::string("RESET expects 0 arguments"));
}

TEST(Registry, UnknownAndEmpty) {
    EXPECT_EQ(parse_failure("FROB"), std::string("unknown command: FROB"));
    EXPECT_EQ(parse_failure(""), std::string("empty command"));
}

TEST(Registry, ConversionErrorNamesTheToken) {
    EXPECT_EQ(parse_failure("ADD 12abc"), std::string("not a number: 12abc"));
    EXPECT_EQ(parse_failure("ADD 99999999999999999999"),
              std::string("number out of range: 99999999999999999999"));
}

TEST(Registry, ConversionHappensAtBindNotAtCall) {
    // Разбор и исполнение разделены: ошибка аргумента — ParseError при bind,
    // до того как обработчик тронул контекст.
    const std::string line = "ADD nope";
    const kvs::Tokens tokens = kvs::tokenize(line);
    EXPECT_THROW(static_cast<void>(registry().bind(tokens, line)), kvs::ParseError);
}

TEST(Registry, BoundCommandKnowsItsEffect) {
    const std::string read = "GREET x";
    const std::string write = "ADD 1";
    const kvs::Tokens read_tokens = kvs::tokenize(read);
    const kvs::Tokens write_tokens = kvs::tokenize(write);
    EXPECT_FALSE(registry().bind(read_tokens, read).writes());
    EXPECT_TRUE(registry().bind(write_tokens, write).writes());
    EXPECT_EQ(registry().bind(write_tokens, write).name(), std::string_view("ADD"));
}

TEST(Registry, ArgumentsBorrowFromTheLine) {
    // Аргумент string_view смотрит внутрь исходной строки — правило этапа 2.
    CommandRegistry<std::string_view> echo;
    echo.add("ECHO", Effect::Reads, [](std::string_view& out, std::string_view text) {
        out = text;
        return Response{true, "OK"};
    });
    const std::string line = "ECHO payload";
    const kvs::Tokens tokens = kvs::tokenize(line);
    std::string_view captured;
    static_cast<void>(echo.bind(tokens, line)(captured));
    EXPECT_EQ(captured.data(), line.data() + 5);
}

TEST(Registry, DuplicateNameIsALogicError) {
    // Повтор имени обычного реестра виден только при запуске. Звёздочка —
    // StaticRegistry — ловит его при компиляции; tests/test_static_registry.cpp.
    CommandRegistry<Counter> table;
    table.add("ADD", Effect::Writes, add);
    EXPECT_THROW(table.add("add", Effect::Writes, add), std::logic_error);
}

TEST(Registry, ErrorInHandlerGetsContext) {
    CommandRegistry<Counter> table;
    table.add("FAIL", Effect::Reads, [](Counter&, std::string_view what) -> Response {
        throw kvs::KeyNotFoundError(std::string(what));
    });
    const std::string line = "FAIL thing";
    const kvs::Tokens tokens = kvs::tokenize(line);
    Counter counter;
    try {
        static_cast<void>(table.bind(tokens, line)(counter));
        ADD_FAILURE() << "handler must throw";
    } catch (...) {
        const auto context = kvs::context_of(std::current_exception());
        ASSERT_EQ(context.size(), 1u);
        EXPECT_EQ(context.front(), std::string("while executing FAIL thing"));
    }
}

TEST(Registry, NamesAreSorted) {
    const std::vector<std::string> expected = {"ADD", "COLLECT", "GREET", "RESET"};
    EXPECT_EQ(registry().names(), expected);
}

// Концепция CommandHandler — при компиляции. Негативный случай с полной
// диагностикой — tests/compile_fail/bad_handler.cpp.
static_assert(kvs::CommandHandler<decltype(&add), Counter>);
static_assert(kvs::CommandHandler<decltype(&collect), Counter>);
static_assert(!kvs::CommandHandler<decltype(&add), int>);                 // чужой контекст
static_assert(!kvs::CommandHandler<Response (*)(Counter&, bool), Counter>);   // bool не разбирается
static_assert(!kvs::CommandHandler<Response (*)(Counter&, Rest, int), Counter>);   // Rest не последним
static_assert(!kvs::CommandHandler<
              Response (*)(Counter&, std::optional<int>, int), Counter>);   // обязательный после необязательного
static_assert(!kvs::CommandHandler<int (*)(Counter&), Counter>);           // результат не Response
