// Звёздочка этапа 6: реестр с именами команд в параметрах шаблона.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "kvs/lexer.hpp"
#include "kvs/static_registry.hpp"

namespace {

struct Tally {
    std::int64_t value = 0;
};

kvs::Response bump(Tally& tally, std::int64_t by) {
    tally.value += by;
    return kvs::Response{true, std::to_string(tally.value)};
}

kvs::Response show(Tally& tally) {
    return kvs::Response{true, std::to_string(tally.value)};
}

using Commands = kvs::StaticRegistry<Tally,
                                     kvs::Command<"BUMP", bump, kvs::Effect::Writes>,
                                     kvs::Command<"SHOW", show, kvs::Effect::Reads>>;

// Состав реестра известен компилятору.
static_assert(Commands::size() == 2);
static_assert(Commands::contains("BUMP"));
static_assert(Commands::contains("show"));      // без учёта регистра, как и bind
static_assert(!Commands::contains("DROP"));

// FixedString — структурный тип: N выводится из литерала.
static_assert(kvs::FixedString("GET").view() == "GET");
static_assert(sizeof(kvs::FixedString("GET").text) == 4);   // с завершающим нулём

kvs::Response run(const Commands& commands, Tally& tally, std::string_view line) {
    const kvs::Tokens tokens = kvs::tokenize(line);
    return commands.bind(tokens, line)(tally);
}

}  // namespace

TEST(StaticRegistry, BehavesLikeTheDynamicOne) {
    const Commands commands;
    Tally tally;
    EXPECT_EQ(run(commands, tally, "BUMP 5").text, std::string("5"));
    EXPECT_EQ(run(commands, tally, "bump -2").text, std::string("3"));
    EXPECT_EQ(run(commands, tally, "SHOW").text, std::string("3"));
    EXPECT_THROW(static_cast<void>(run(commands, tally, "BUMP")), kvs::ParseError);
    EXPECT_THROW(static_cast<void>(run(commands, tally, "DROP")), kvs::ParseError);
}

// Повтор имени — tests/compile_fail/duplicate_command.cpp: та же пара
// команд с «BUMP» и «bump» не собирается.
