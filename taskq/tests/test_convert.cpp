#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "taskq/convert.hpp"
#include "taskq/errors.hpp"
#include "taskq/function_traits.hpp"
#include "taskq/legacy_convert.hpp"

using taskq::convert;
using taskq::TaskId;

namespace {

// Причина отказа разбора, или «(ok)».
template <class Convert>
std::string failure_of(Convert convert_one, std::string_view token) {
    try {
        static_cast<void>(convert_one(token));
    } catch (const taskq::ParseError& e) {
        return e.reason();
    }
    return "(ok)";
}

}  // namespace

TEST(Convert, PriorityRange) {
    const auto to_priority = [](std::string_view t) { return convert<taskq::Priority>(t); };
    EXPECT_EQ(convert<taskq::Priority>("9").value, 9);
    EXPECT_EQ(failure_of(to_priority, "10"), std::string("priority must be 0..9: 10"));
    EXPECT_EQ(failure_of(to_priority, "-1"), std::string("priority must be 0..9: -1"));
}

TEST(Convert, TaskIdBranchComesBeforeNumbers) {
    // TaskId — псевдоним uint64_t. Ветка TaskId выше ветки чисел, поэтому
    // формат номера проверяется. Будь ветки в обратном порядке, «+7» и «007»
    // прошли бы как 7: is_arithmetic_v<TaskId> истинно.
    const auto to_id = [](std::string_view t) { return convert<TaskId>(t); };
    EXPECT_EQ(convert<TaskId>("42"), 42u);
    for (const std::string_view bad : {"+7", "007", "0", "", "7x", "-1", "99999999999999999999"}) {
        EXPECT_EQ(failure_of(to_id, bad), "task id must be a positive number: " + std::string(bad)) << bad;
    }
    // Другие числа — по общим правилам from_chars: ведущий ноль им не помеха.
    EXPECT_EQ(convert<std::int64_t>("007"), 7);
}

TEST(Convert, DelayWithUnits) {
    using std::chrono::seconds;
    EXPECT_EQ(convert<seconds>("90"), seconds(90));
    EXPECT_EQ(convert<seconds>("90s"), seconds(90));
    EXPECT_EQ(convert<seconds>("15m"), seconds(900));
    EXPECT_EQ(convert<seconds>("2h"), seconds(7200));
    const auto to_delay = [](std::string_view t) { return convert<seconds>(t); };
    for (const std::string_view bad : {"", "m", "-5", "5d", "1.5h", "8761h"}) {
        EXPECT_NE(failure_of(to_delay, bad), std::string("(ok)")) << bad;
    }
}

TEST(Convert, NamesAndKinds) {
    EXPECT_EQ(convert<taskq::QueueName>("build tasks").name, std::string_view("build tasks"));
    EXPECT_EQ(failure_of([](std::string_view t) { return convert<taskq::QueueName>(t); }, " build"),
              std::string("queue name starts or ends with a space"));
    EXPECT_EQ(convert<taskq::Kind>("number").name, std::string_view("number"));
    EXPECT_EQ(failure_of([](std::string_view t) { return convert<taskq::Kind>(t); }, "json"),
              std::string("type must be text, number or binary: json"));
}

TEST(Convert, ViewsDoNotCopy) {
    const std::string_view token = "build";
    EXPECT_EQ(convert<taskq::QueueName>(token).name.data(), token.data());
    EXPECT_EQ(convert<std::string_view>(token).data(), token.data());
}

TEST(Convert, BatchFromTokens) {
    const taskq::Batch batch = taskq::Batch::from_tokens({"build", "a", "deploy", "prio=7", "b"});
    ASSERT_EQ(batch.items.size(), 2u);
    EXPECT_EQ(batch.items[1].queue, std::string_view("deploy"));
    EXPECT_EQ(batch.items[1].priority, 7);
    EXPECT_THROW(static_cast<void>(taskq::Batch::from_tokens({})), taskq::ParseError);
}

TEST(Convert, LegacyAgreesWithModern) {
    // Историческая версия на enable_if и новая на if constexpr — одно и то же
    // поведение на одних и тех же токенах.
    for (const std::string_view token : {"1", "42", "007", "+7", "0", "x", ""}) {
        EXPECT_EQ(failure_of([](std::string_view t) { return convert<TaskId>(t); }, token),
                  failure_of([](std::string_view t) { return taskq::legacy::convert<TaskId>(t); }, token))
            << token;
    }
    for (const std::string_view token : {"0", "9", "10", "-1", "5x"}) {
        EXPECT_EQ(failure_of([](std::string_view t) { return convert<taskq::Priority>(t); }, token),
                  failure_of([](std::string_view t) { return taskq::legacy::convert<taskq::Priority>(t); },
                             token))
            << token;
    }
    for (const std::string_view token : {"-3", "12abc", "007"}) {
        EXPECT_EQ(failure_of([](std::string_view t) { return convert<std::int64_t>(t); }, token),
                  failure_of([](std::string_view t) { return taskq::legacy::convert<std::int64_t>(t); },
                             token))
            << token;
    }
    EXPECT_EQ(taskq::legacy::convert<std::string_view>("x"), convert<std::string_view>("x"));
}

// Закрытый список типов.
static_assert(taskq::Argument<taskq::QueueName>);
static_assert(taskq::Argument<std::optional<taskq::Priority>>);
static_assert(taskq::Argument<taskq::Tail>);
static_assert(taskq::Argument<taskq::Batch>);
static_assert(!taskq::Argument<std::optional<taskq::Tail>>);
static_assert(!taskq::Argument<bool>);
static_assert(!taskq::Argument<std::string>);
static_assert(taskq::Named<taskq::Priority> && !taskq::Named<taskq::QueueName>);

// --- function_traits ----------------------------------------------------

// Только объявления: function_traits смотрит на типы, и определения не
// нужны. Пространство имён именованное, а не анонимное: объявленная, но не
// определённая функция с внутренним связыванием — предупреждение.
namespace samples {

int free_function(double, const std::string&);

struct Member {
    void change(int);
    bool query() const noexcept;
};

}  // namespace samples

using namespace samples;

template <class F>
using traits = taskq::function_traits<F>;

template <class F>
concept HasSignature = requires { typename taskq::function_traits<F>::result; };

static_assert(std::is_same_v<traits<decltype(&free_function)>::result, int>);
static_assert(std::is_same_v<traits<decltype(&free_function)>::arguments,
                             std::tuple<double, const std::string&>>);
static_assert(std::is_void_v<traits<decltype(&free_function)>::object>);
// У метода — объект с константностью метода: так реестр понимает, что
// STATUS только читает.
static_assert(std::is_same_v<traits<decltype(&Member::change)>::object, Member>);
static_assert(std::is_same_v<traits<decltype(&Member::query)>::object, const Member>);
static_assert(std::is_same_v<traits<decltype(&Member::change)>::arguments, std::tuple<int>>);

TEST(FunctionTraits, LambdaAndGenericLambda) {
    const auto lambda = [](std::string_view, int) { return 1.5; };
    static_assert(std::is_same_v<traits<decltype(lambda)>::result, double>);
    static_assert(std::is_void_v<traits<decltype(lambda)>::object>);
    const auto generic = [](auto value) { return value; };
    static_assert(!HasSignature<decltype(generic)>);
    SUCCEED();
}
