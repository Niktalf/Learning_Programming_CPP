#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "kvs/convert.hpp"
#include "kvs/errors.hpp"
#include "kvs/function_traits.hpp"
#include "kvs/legacy_convert.hpp"

using kvs::convert;

namespace {

// Причина отказа разбора, или «(ok)».
template <class Convert>
std::string failure_of(Convert convert_one, std::string_view token) {
    try {
        static_cast<void>(convert_one(token));
    } catch (const kvs::ParseError& e) {
        return e.reason();
    }
    return "(ok)";
}

}  // namespace

TEST(Convert, Strings) {
    const std::string_view token = "hello";
    EXPECT_EQ(convert<std::string_view>(token).data(), token.data());   // без копии
    EXPECT_EQ(convert<std::string>(token), std::string("hello"));
}

TEST(Convert, Numbers) {
    EXPECT_EQ(convert<std::int64_t>("-42"), -42);
    EXPECT_EQ(convert<std::size_t>("17"), 17u);
    EXPECT_DOUBLE_EQ(convert<double>("2.5"), 2.5);
}

TEST(Convert, TrailingGarbageIsRejected) {
    // from_chars разбирает префикс «12» и сообщает об успехе. Без проверки
    // конечного указателя «12abc» стало бы числом 12.
    const auto to_int = [](std::string_view token) { return convert<std::int64_t>(token); };
    EXPECT_EQ(failure_of(to_int, "12abc"), std::string("not a number: 12abc"));
    EXPECT_EQ(failure_of(to_int, ""), std::string("not a number: "));
    EXPECT_EQ(failure_of(to_int, "+5"), std::string("not a number: +5"));
    EXPECT_EQ(failure_of(to_int, "9223372036854775808"),
              std::string("number out of range: 9223372036854775808"));
}

TEST(Convert, NegativeIntoUnsignedIsRejected) {
    const auto to_size = [](std::string_view token) { return convert<std::size_t>(token); };
    EXPECT_EQ(failure_of(to_size, "-1"), std::string("not a number: -1"));
}

TEST(Convert, ErrorInputIsTheToken) {
    try {
        static_cast<void>(convert<std::int64_t>("abc"));
        ADD_FAILURE() << "must throw";
    } catch (const kvs::ParseError& e) {
        EXPECT_EQ(e.input(), std::string("abc"));
    }
}

TEST(Convert, LegacyAgreesWithModern) {
    // Историческая версия на enable_if и новая на if constexpr — одно и то же
    // поведение на одних и тех же токенах.
    for (const std::string_view token : {"0", "42", "-7", "12abc", "", "x", "99999999999999999999"}) {
        const std::string modern =
            failure_of([](std::string_view t) { return convert<std::int64_t>(t); }, token);
        const std::string legacy = failure_of(
            [](std::string_view t) { return kvs::legacy::convert<std::int64_t>(t); }, token);
        EXPECT_EQ(modern, legacy) << token;
        if (modern == "(ok)") {
            EXPECT_EQ(convert<std::int64_t>(token), kvs::legacy::convert<std::int64_t>(token));
        }
    }
    EXPECT_EQ(kvs::legacy::convert<std::string>("abc"), convert<std::string>("abc"));
    EXPECT_EQ(kvs::legacy::convert<std::optional<std::int64_t>>(""), std::nullopt);
    EXPECT_EQ(kvs::legacy::convert<std::optional<std::int64_t>>("5"), std::optional<std::int64_t>(5));
}

// Закрытый список типов: bool и произвольный класс не проходят концепцию.
static_assert(kvs::Argument<std::string_view>);
static_assert(kvs::Argument<std::optional<std::int64_t>>);
static_assert(kvs::Argument<kvs::Rest>);
static_assert(!kvs::Argument<bool>);
static_assert(!kvs::Argument<std::optional<bool>>);
static_assert(!kvs::Argument<std::optional<kvs::Rest>>);
static_assert(!kvs::Argument<const std::string&>);

// --- function_traits ----------------------------------------------------

// Только объявления: function_traits смотрит на типы, и определения не
// нужны. Пространство имён именованное, а не анонимное: объявленная, но не
// определённая функция с внутренним связыванием — предупреждение.
namespace samples {

int free_function(double, const std::string&);
int noexcept_function(char) noexcept;

struct Functor {
    long operator()(int) const;
};

struct Member {
    void change(int);
    bool query() const noexcept;
};

}  // namespace samples

using namespace samples;

template <class F>
concept HasSignature = requires { typename kvs::function_traits<F>::result; };

template <class F>
using result_of = typename kvs::function_traits<F>::result;
template <class F>
using arguments_of = typename kvs::function_traits<F>::arguments;

static_assert(std::is_same_v<result_of<decltype(&free_function)>, int>);
static_assert(std::is_same_v<arguments_of<decltype(&free_function)>,
                             std::tuple<double, const std::string&>>);
static_assert(std::is_same_v<arguments_of<decltype(free_function)>,
                             std::tuple<double, const std::string&>>);   // тип функции, не указатель
static_assert(std::is_same_v<arguments_of<decltype(&noexcept_function)>, std::tuple<char>>);
static_assert(std::is_same_v<result_of<Functor>, long>);
static_assert(std::is_same_v<arguments_of<Functor>, std::tuple<int>>);
static_assert(std::is_same_v<arguments_of<decltype(&Member::change)>, std::tuple<int>>);
static_assert(std::is_same_v<result_of<decltype(&Member::query)>, bool>);

TEST(FunctionTraits, Lambda) {
    const auto lambda = [](std::string_view, int) { return 1.5; };
    static_assert(std::is_same_v<result_of<decltype(lambda)>, double>);
    static_assert(std::is_same_v<arguments_of<decltype(lambda)>, std::tuple<std::string_view, int>>);
    static_assert(kvs::function_traits<decltype(lambda)>::arity == 2);
    SUCCEED();
}

TEST(FunctionTraits, GenericLambdaHasNoSignature) {
    // У обобщённой лямбды operator() — шаблон, и взять его адрес без
    // аргументов нельзя: сигнатуры нет, и реестр такой обработчик отвергнет.
    const auto generic = [](auto value) { return value; };
    static_assert(!HasSignature<decltype(generic)>);
    static_assert(HasSignature<decltype(&Functor::operator())>);
    SUCCEED();
}
