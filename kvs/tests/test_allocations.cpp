// Звёздочка этапа 2 в виде теста. Отдельная программа: она подменяет
// глобальный operator new, а это можно сделать только один раз на всю
// программу — в общем наборе тестов подмена считала бы и чужие аллокации.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "alloc_counter.hpp"
#include "kvs/lexer.hpp"
#include "naive_lexer.hpp"
#include "workload.hpp"

namespace {

// Под санитайзером адресов подсчёт недоступен, см. alloc_counter.hpp.
#define SKIP_WITHOUT_COUNTING()                                              \
    if (!kvs::tools::counting_available()) {                                \
        GTEST_SKIP() << "allocation counting is disabled under AddressSanitizer"; \
    }

}  // namespace

TEST(Allocations, ViewLexerAllocatesOnlyForTheVector) {
    SKIP_WITHOUT_COUNTING();
    const std::vector<std::string> lines = kvs::tools::sample_commands();

    kvs::tools::reset_allocations();
    for (const std::string& line : lines) {
        const kvs::Tokens tokens = kvs::tokenize(line);
        EXPECT_FALSE(tokens.empty());
    }
    const std::size_t viewing = kvs::tools::allocations();

    // Не больше трёх на команду: вектор растёт 1 → 2 → 4.
    EXPECT_LE(viewing, 3 * lines.size());
}

TEST(Allocations, CopyingLexerPaysForLongTokens) {
    SKIP_WITHOUT_COUNTING();
    const std::vector<std::string> lines = kvs::tools::sample_commands();

    kvs::tools::reset_allocations();
    for (const std::string& line : lines) {
        EXPECT_FALSE(kvs::tools::tokenize_copying(line).empty());
    }
    const std::size_t copying = kvs::tools::allocations();

    kvs::tools::reset_allocations();
    for (const std::string& line : lines) {
        EXPECT_FALSE(kvs::tokenize(line).empty());
    }
    const std::size_t viewing = kvs::tools::allocations();

    // Каждое третье значение SET длиннее буфера коротких строк.
    EXPECT_GE(copying, viewing + lines.size() / 3);
}

TEST(Allocations, ShortTokensFitIntoTheStringItself) {
    SKIP_WITHOUT_COUNTING();
    const std::string line = "GET k";
    kvs::tools::reset_allocations();
    const std::vector<std::string> tokens = kvs::tools::tokenize_copying(line);
    // Два коротких токена — только вектор (1 → 2), сами строки в куче не живут.
    EXPECT_EQ(tokens.size(), 2u);
    EXPECT_EQ(kvs::tools::allocations(), 2u);
}
