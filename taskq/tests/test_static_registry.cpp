// Звёздочка этапа 6: реестр с именами команд в параметрах шаблона.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "taskq/static_registry.hpp"

namespace {

// Контекст с методами — как Service: обработчики-методы работают и здесь.
class Tally {
public:
    taskq::Response bump(std::int64_t by) {
        value_ += by;
        return taskq::Response{true, std::to_string(value_)};
    }

    taskq::Response show() const { return taskq::Response{true, std::to_string(value_)}; }

private:
    std::int64_t value_ = 0;
};

using Commands =
    taskq::StaticRegistry<Tally, taskq::Command<"BUMP", &Tally::bump>, taskq::Command<"SHOW", &Tally::show>>;

// Состав реестра известен компилятору.
static_assert(Commands::size() == 2);
static_assert(Commands::contains("BUMP"));
static_assert(Commands::contains("show"));      // без учёта регистра, как и bind
static_assert(!Commands::contains("DROP"));

// FixedString — структурный тип: N выводится из литерала.
static_assert(taskq::FixedString("ACK").view() == "ACK");
static_assert(sizeof(taskq::FixedString("ACK").text) == 4);   // с завершающим нулём

}  // namespace

TEST(StaticRegistry, BehavesLikeTheDynamicOne) {
    const Commands commands;
    Tally tally;
    EXPECT_EQ(commands.bind("BUMP 5")(tally).text, std::string("5"));
    EXPECT_EQ(commands.bind("bump -2")(tally).text, std::string("3"));
    EXPECT_EQ(commands.bind("SHOW")(tally).text, std::string("3"));
    EXPECT_ANY_THROW(static_cast<void>(commands.bind("BUMP")));
    EXPECT_THROW(static_cast<void>(commands.bind("DROP")), taskq::ParseError);
}

// Повтор имени — tests/compile_fail/duplicate_command.cpp: пара «ACK» и
// «ack» не собирается.
