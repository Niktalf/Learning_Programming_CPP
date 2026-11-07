#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "chat/convert.hpp"
#include "chat/errors.hpp"
#include "chat/function_traits.hpp"
#include "chat/legacy_convert.hpp"

using chat::convert;

namespace {

// Причина отказа разбора, или «(ok)».
template <class Convert>
std::string failure_of(Convert convert_one, std::string_view token) {
    try {
        static_cast<void>(convert_one(token));
    } catch (const chat::ParseError& e) {
        return e.reason();
    }
    return "(ok)";
}

}  // namespace

TEST(Convert, TypedNamesAreValidated) {
    EXPECT_EQ(convert<chat::RoomName>("#general").name, std::string_view("#general"));
    EXPECT_EQ(failure_of([](std::string_view t) { return convert<chat::RoomName>(t); }, "general"),
              std::string("room name must start with #: general"));
    EXPECT_EQ(convert<chat::Nick>("alice").name, std::string_view("alice"));
    EXPECT_EQ(failure_of([](std::string_view t) { return convert<chat::Nick>(t); }, "a b"),
              std::string("nickname may contain only letters, digits, _ and -"));
}

TEST(Convert, RoomListSplitsAndChecks) {
    const chat::RoomList list = convert<chat::RoomList>("#a,#b,#c");
    ASSERT_EQ(list.rooms.size(), 3u);
    EXPECT_EQ(list.rooms[1], std::string_view("#b"));
    EXPECT_EQ(failure_of([](std::string_view t) { return convert<chat::RoomList>(t); }, "#a,#a"),
              std::string("room listed twice: #a"));
}

TEST(Convert, ViewsDoNotCopy) {
    const std::string_view token = "#general";
    EXPECT_EQ(convert<chat::RoomName>(token).name.data(), token.data());
    EXPECT_EQ(convert<std::string_view>(token).data(), token.data());
}

TEST(Convert, Numbers) {
    const auto to_index = [](std::string_view t) { return convert<std::size_t>(t); };
    const auto to_step = [](std::string_view t) { return convert<std::int64_t>(t); };
    EXPECT_EQ(convert<std::size_t>("17"), 17u);
    EXPECT_EQ(convert<std::int64_t>("-5"), -5);
    // from_chars разбирает префикс «12» и сообщает об успехе. Без проверки
    // конечного указателя «12abc» стало бы числом 12.
    EXPECT_EQ(failure_of(to_step, "12abc"), std::string("not a number: 12abc"));
    EXPECT_EQ(failure_of(to_index, "-1"), std::string("not a non-negative number: -1"));
    EXPECT_EQ(failure_of(to_index, "18446744073709551616"),
              std::string("number out of range: 18446744073709551616"));
}

TEST(Convert, ErrorInputIsTheToken) {
    try {
        static_cast<void>(convert<chat::RoomName>("general"));
        ADD_FAILURE() << "must throw";
    } catch (const chat::ParseError& e) {
        EXPECT_EQ(e.input(), std::string("general"));
    }
}

TEST(Convert, LegacyAgreesWithModern) {
    // Историческая версия на enable_if и новая на if constexpr — одно и то же
    // поведение на одних и тех же токенах.
    for (const std::string_view token : {"0", "42", "-7", "12abc", "", "99999999999999999999"}) {
        const std::string modern =
            failure_of([](std::string_view t) { return convert<std::size_t>(t); }, token);
        const std::string legacy =
            failure_of([](std::string_view t) { return chat::legacy::convert<std::size_t>(t); }, token);
        EXPECT_EQ(modern, legacy) << token;
    }
    for (const std::string_view token : {"#ok", "bad", "#", "#a b"}) {
        const std::string modern =
            failure_of([](std::string_view t) { return convert<chat::RoomName>(t); }, token);
        const std::string legacy =
            failure_of([](std::string_view t) { return chat::legacy::convert<chat::RoomName>(t); }, token);
        EXPECT_EQ(modern, legacy) << token;
    }
    EXPECT_EQ(chat::legacy::convert<std::string_view>("x"), convert<std::string_view>("x"));
}

TEST(Convert, Placeholders) {
    static_assert(chat::placeholder<chat::RoomName>() == "#room");
    static_assert(chat::placeholder<chat::Tail>() == "text...");
    static_assert(chat::placeholder<std::size_t>() == "number");
    SUCCEED();
}

// Закрытый список типов.
static_assert(chat::Argument<chat::RoomName>);
static_assert(chat::Argument<std::optional<chat::Nick>>);
static_assert(chat::Argument<chat::Tail>);
static_assert(!chat::Argument<std::optional<chat::Tail>>);
static_assert(!chat::Argument<bool>);
static_assert(!chat::Argument<std::string>);       // копия не нужна: есть string_view
static_assert(!chat::Argument<const char*>);

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
using result_of = typename chat::function_traits<F>::result;
template <class F>
using arguments_of = typename chat::function_traits<F>::arguments;

template <class F>
concept HasSignature = requires { typename chat::function_traits<F>::result; };

static_assert(std::is_same_v<result_of<decltype(&free_function)>, int>);
static_assert(std::is_same_v<arguments_of<decltype(&free_function)>,
                             std::tuple<double, const std::string&>>);
static_assert(std::is_same_v<arguments_of<decltype(free_function)>,
                             std::tuple<double, const std::string&>>);   // тип функции, не указатель
static_assert(std::is_same_v<arguments_of<decltype(&noexcept_function)>, std::tuple<char>>);
static_assert(std::is_same_v<result_of<Functor>, long>);
static_assert(std::is_same_v<arguments_of<decltype(&Member::change)>, std::tuple<int>>);
static_assert(std::is_same_v<result_of<decltype(&Member::query)>, bool>);
static_assert(chat::function_traits<decltype(&free_function)>::arity == 2);

TEST(FunctionTraits, LambdaAndGenericLambda) {
    const auto lambda = [](std::string_view, int) { return 1.5; };
    static_assert(std::is_same_v<result_of<decltype(lambda)>, double>);
    static_assert(std::is_same_v<arguments_of<decltype(lambda)>, std::tuple<std::string_view, int>>);
    // У обобщённой лямбды operator() — шаблон, и адреса без аргументов у
    // него нет: сигнатуры нет, и реестр такой обработчик отвергнет.
    const auto generic = [](auto value) { return value; };
    static_assert(!HasSignature<decltype(generic)>);
    SUCCEED();
}
