// Второй путь этапа 3: ошибка, возвращённая значением, против исключения.
// Тесты показывают поведение обоих путей на одних и тех же входах и то, во что
// обходится проброс ошибки через несколько уровней вызовов.

#include <gtest/gtest.h>

#include <string>
#include <variant>

#include "kvs/errors.hpp"
#include "kvs/parser.hpp"
#include "kvs/result.hpp"

using kvs::InputLine;
using kvs::ParseError;
using kvs::Result;
using kvs::Unexpected;

TEST(Result, HoldsValueOrError) {
    const Result<int, std::string> good = 42;
    const Result<int, std::string> bad = Unexpected{std::string("broken")};

    ASSERT_TRUE(good.has_value());
    EXPECT_EQ(*good, 42);
    ASSERT_FALSE(bad);
    EXPECT_EQ(bad.error(), std::string("broken"));
}

TEST(Result, SameTypeForValueAndErrorIsUnambiguous) {
    // Ради этого случая и нужен Unexpected: иначе непонятно, что передали.
    const Result<std::string, std::string> value = std::string("ok");
    const Result<std::string, std::string> error = Unexpected{std::string("fail")};
    EXPECT_TRUE(value.has_value());
    EXPECT_FALSE(error.has_value());
}

TEST(Result, ValueThrowsTheStoredError) {
    const Result<int, ParseError> bad = Unexpected{ParseError("reason", "input")};
    EXPECT_THROW(static_cast<void>(bad.value()), ParseError);
}

TEST(TryParse, AgreesWithParseOnValidInput) {
    for (const char* const text :
         {"SET k v", "GET k", "DEL k", "EXISTS k", "TYPE k", "INCR k 5", "MSET a 1 b 2"}) {
        const InputLine line{text};
        const auto result = kvs::try_parse(line);
        ASSERT_TRUE(result.has_value()) << text;
        EXPECT_EQ(result->index(), kvs::parse(line).index()) << text;
    }
}

TEST(TryParse, ReturnsTheSameReasonParseThrows) {
    for (const char* const text :
         {"", "FOO", "SET k", "INCR k 12abc", "MSET a", R"(SET k "open)"}) {
        const InputLine line{text};
        const auto result = kvs::try_parse(line);
        ASSERT_FALSE(result.has_value()) << text;
        try {
            static_cast<void>(kvs::parse(line));
            ADD_FAILURE() << "parse must throw on " << text;
        } catch (const ParseError& e) {
            EXPECT_EQ(result.error().reason(), e.reason()) << text;
        }
    }
}

// Проброс ошибки через десять уровней — третий пункт сравнения из
// docs/errors.md. Исключение пролетает все уровни само; ошибку-значение
// каждый уровень обязан проверить и передать дальше вручную.
namespace {

int throwing_level(int depth) {
    if (depth == 0) {
        throw ParseError("deep failure", "level 0");
    }
    return throwing_level(depth - 1) + 1;   // ни строки про ошибку
}

Result<int, ParseError> returning_level(int depth) {
    if (depth == 0) {
        return Unexpected{ParseError("deep failure", "level 0")};
    }
    const auto below = returning_level(depth - 1);
    if (!below) {
        return Unexpected{below.error()};   // и так на каждом из десяти уровней
    }
    return *below + 1;
}

}  // namespace

TEST(TryParse, ErrorThroughTenLevels) {
    EXPECT_THROW(throwing_level(10), ParseError);

    const auto result = returning_level(10);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().reason(), std::string("deep failure"));
}
