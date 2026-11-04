// Звёздочка этапа 2 в виде теста. Отдельная программа: она подменяет
// глобальный operator new, а это можно сделать только один раз на всю
// программу — в общем наборе тестов подмена считала бы и чужие аллокации.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "alloc_counter.hpp"
#include "chat/lexer.hpp"
#include "naive_lexer.hpp"
#include "workload.hpp"

namespace {

// Под санитайзером адресов подсчёт недоступен, см. alloc_counter.hpp.
#define SKIP_WITHOUT_COUNTING()                                              \
    if (!chat::tools::counting_available()) {                                \
        GTEST_SKIP() << "allocation counting is disabled under AddressSanitizer"; \
    }

}  // namespace

TEST(Allocations, ViewLexerAllocatesOnlyForTheVector) {
    SKIP_WITHOUT_COUNTING();
    const std::vector<std::string> lines = chat::tools::sample_commands();

    chat::tools::reset_allocations();
    for (const std::string& line : lines) {
        const chat::Tokens tokens = chat::tokenize(line, chat::tools::limit_for(line));
        EXPECT_FALSE(tokens.empty());
    }
    const std::size_t viewing = chat::tools::allocations();

    // Не больше трёх на команду: вектор растёт 1 → 2 → 4.
    EXPECT_LE(viewing, 3 * lines.size());
}

TEST(Allocations, CopyingLexerPaysForLongTokens) {
    SKIP_WITHOUT_COUNTING();
    const std::vector<std::string> lines = chat::tools::sample_commands();

    chat::tools::reset_allocations();
    for (const std::string& line : lines) {
        EXPECT_FALSE(chat::tools::tokenize_copying(line, chat::tools::limit_for(line)).empty());
    }
    const std::size_t copying = chat::tools::allocations();

    chat::tools::reset_allocations();
    for (const std::string& line : lines) {
        EXPECT_FALSE(chat::tokenize(line, chat::tools::limit_for(line)).empty());
    }
    const std::size_t viewing = chat::tools::allocations();

    // Каждый третий текст /msg длиннее буфера коротких строк.
    EXPECT_GE(copying, viewing + lines.size() / 3);
}

TEST(Allocations, ShortTokensFitIntoTheStringItself) {
    SKIP_WITHOUT_COUNTING();
    const std::string line = "/who #a";
    chat::tools::reset_allocations();
    const std::vector<std::string> tokens = chat::tools::tokenize_copying(line);
    // Два коротких токена — только вектор (1 → 2), сами строки в куче не живут.
    EXPECT_EQ(tokens.size(), 2u);
    EXPECT_EQ(chat::tools::allocations(), 2u);
}
