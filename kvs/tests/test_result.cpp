// Второй путь этапа 3: ошибка, возвращённая значением, против исключения.
// Тесты показывают поведение обоих путей на одних и тех же входах и то, во что
// обходится проброс ошибки через несколько уровней вызовов.
//
// Этап 5: собственный Result заменён на std::expected из C++23. Интерфейс
// почти тот же; отличие одно, и тест Expected.ValueThrowsBadExpectedAccess
// его фиксирует.

#include <gtest/gtest.h>

#include <expected>
#include <string>
#include <variant>

#include "kvs/errors.hpp"
#include "kvs/parser.hpp"

using kvs::InputLine;
using kvs::ParseError;

TEST(Expected, HoldsValueOrError) {
    const std::expected<int, std::string> good = 42;
    const std::expected<int, std::string> bad = std::unexpected(std::string("broken"));

    ASSERT_TRUE(good.has_value());
    EXPECT_EQ(*good, 42);
    ASSERT_FALSE(bad);
    EXPECT_EQ(bad.error(), std::string("broken"));
}

TEST(Expected, SameTypeForValueAndErrorIsUnambiguous) {
    // Ради этого случая и нужен std::unexpected: иначе непонятно, что передали.
    const std::expected<std::string, std::string> value = std::string("ok");
    const std::expected<std::string, std::string> error = std::unexpected(std::string("fail"));
    EXPECT_TRUE(value.has_value());
    EXPECT_FALSE(error.has_value());
}

TEST(Expected, ValueThrowsBadExpectedAccess) {
    // Отличие от Result этапа 3: value() бросает не саму ошибку, а
    // std::bad_expected_access<E>, внутри которой ошибка лежит. Поэтому
    // tokenize больше не сводится к try_tokenize(...).value() — он бросает
    // error() явно, и граница по-прежнему получает ParseError.
    const std::expected<int, ParseError> bad = std::unexpected(ParseError("reason", "input"));
    try {
        static_cast<void>(bad.value());
        ADD_FAILURE() << "value() must throw";
    } catch (const std::bad_expected_access<ParseError>& e) {
        EXPECT_EQ(e.error().reason(), std::string("reason"));
    }
}

TEST(Expected, MonadicChaining) {
    // and_then и transform из C++23: цепочка шагов, каждый из которых может
    // не удаться, без ручной проверки после каждого. Это ответ стандарта на
    // главный недостаток второго пути — ручной проброс через уровни.
    const auto parse_number = [](const std::string& text) -> std::expected<int, std::string> {
        if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
            return std::unexpected("not a number: " + text);
        }
        return std::stoi(text);
    };
    const auto doubled = parse_number("21").transform([](int value) { return value * 2; });
    EXPECT_EQ(*doubled, 42);
    const auto failed = parse_number("x1").transform([](int value) { return value * 2; });
    EXPECT_EQ(failed.error(), std::string("not a number: x1"));
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

std::expected<int, ParseError> returning_level(int depth) {
    if (depth == 0) {
        return std::unexpected(ParseError("deep failure", "level 0"));
    }
    const auto below = returning_level(depth - 1);
    if (!below) {
        return std::unexpected(below.error());   // и так на каждом из десяти уровней
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
