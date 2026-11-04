// Второй путь этапа 3: ошибка, возвращённая значением, против исключения.
// Тесты показывают поведение обоих путей на одних и тех же входах и то, во что
// обходится проброс ошибки через несколько уровней вызовов.

#include <gtest/gtest.h>

#include <string>
#include <variant>

#include "chat/errors.hpp"
#include "chat/parser.hpp"
#include "chat/result.hpp"

using chat::InputLine;
using chat::ParseError;
using chat::Result;
using chat::Unexpected;

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
         {"/join #a", "/leave #a", "/msg #a,#b hi there", "/who #a", "/history #a 3",
          "/type #a 0", "/react #a 1 2", R"(/topic #a "new topic")", "/nick alicia"}) {
        const InputLine line{text};
        const auto result = chat::try_parse(line);
        ASSERT_TRUE(result.has_value()) << text;
        EXPECT_EQ(result->index(), chat::parse(line).index()) << text;
    }
}

TEST(TryParse, ReturnsTheSameReasonParseThrows) {
    for (const char* const text :
         {"", "/frob #a", "/join a", "/msg #a", "/msg #a,#a hi", "/history #a 3x",
          R"(/topic #a "open)", "/nick", "/msg #a \xd0"}) {
        const InputLine line{text};
        const auto result = chat::try_parse(line);
        ASSERT_FALSE(result.has_value()) << text;
        try {
            static_cast<void>(chat::parse(line));
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
