// Звёздочка этапа 6: реестр с именами команд в параметрах шаблона.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "chat/static_registry.hpp"

namespace {

struct Tally {
    std::int64_t value = 0;
};

chat::Response bump(Tally& tally, std::int64_t by) {
    tally.value += by;
    return chat::Response{true, std::to_string(tally.value)};
}

chat::Response show(Tally& tally) {
    return chat::Response{true, std::to_string(tally.value)};
}

using Commands = chat::StaticRegistry<Tally, chat::Command<"/bump", bump>, chat::Command<"/show", show>>;

// Состав реестра известен компилятору.
static_assert(Commands::size() == 2);
static_assert(Commands::contains("/bump"));
static_assert(Commands::contains("/SHOW"));     // без учёта регистра, как и bind
static_assert(!Commands::contains("/drop"));

// FixedString — структурный тип: N выводится из литерала.
static_assert(chat::FixedString("/join").view() == "/join");
static_assert(sizeof(chat::FixedString("/join").text) == 6);   // с завершающим нулём

}  // namespace

TEST(StaticRegistry, BehavesLikeTheDynamicOne) {
    const Commands commands;
    Tally tally;
    EXPECT_EQ(commands.bind("/bump 5")(tally).text, std::string("5"));
    EXPECT_EQ(commands.bind("/BUMP -2")(tally).text, std::string("3"));
    EXPECT_EQ(commands.bind("/show")(tally).text, std::string("3"));
    EXPECT_THROW(static_cast<void>(commands.bind("/bump")), chat::ParseError);
    EXPECT_THROW(static_cast<void>(commands.bind("/drop")), chat::ParseError);
}

// Повтор имени — tests/compile_fail/duplicate_command.cpp: пара «/bump» и
// «/BUMP» не собирается.
