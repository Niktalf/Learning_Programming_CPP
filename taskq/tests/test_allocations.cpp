// Звёздочка этапа 2 в виде теста. Отдельная программа: она подменяет
// глобальный operator new, а это можно сделать только один раз на всю
// программу — в общем наборе тестов подмена считала бы и чужие аллокации.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "alloc_counter.hpp"
#include "taskq/lexer.hpp"
#include "naive_lexer.hpp"
#include "workload.hpp"

namespace {

// Под санитайзером адресов подсчёт недоступен, см. alloc_counter.hpp.
#define SKIP_WITHOUT_COUNTING()                                              \
    if (!taskq::tools::counting_available()) {                                \
        GTEST_SKIP() << "allocation counting is disabled under AddressSanitizer"; \
    }

}  // namespace

TEST(Allocations, ViewLexerAllocatesOnlyForTheVector) {
    SKIP_WITHOUT_COUNTING();
    const std::vector<std::string> lines = taskq::tools::sample_commands();

    taskq::tools::reset_allocations();
    for (const std::string& line : lines) {
        const taskq::Tokens tokens = taskq::tokenize(line, taskq::tools::limit_for(line));
        EXPECT_FALSE(tokens.empty());
    }
    const std::size_t viewing = taskq::tools::allocations();

    // Не больше трёх на команду: вектор растёт 1 → 2 → 4.
    EXPECT_LE(viewing, 3 * lines.size());
}

TEST(Allocations, CopyingLexerPaysForLongTokens) {
    SKIP_WITHOUT_COUNTING();
    const std::vector<std::string> lines = taskq::tools::sample_commands();

    taskq::tools::reset_allocations();
    for (const std::string& line : lines) {
        EXPECT_FALSE(taskq::tools::tokenize_copying(line, taskq::tools::limit_for(line)).empty());
    }
    const std::size_t copying = taskq::tools::allocations();

    taskq::tools::reset_allocations();
    for (const std::string& line : lines) {
        EXPECT_FALSE(taskq::tokenize(line, taskq::tools::limit_for(line)).empty());
    }
    const std::size_t viewing = taskq::tools::allocations();

    // Каждое третье тело SUBMIT длиннее буфера коротких строк.
    EXPECT_GE(copying, viewing + lines.size() / 3);
}

TEST(Allocations, ShortTokensFitIntoTheStringItself) {
    SKIP_WITHOUT_COUNTING();
    const std::string line = "POLL q";
    taskq::tools::reset_allocations();
    const std::vector<std::string> tokens = taskq::tools::tokenize_copying(line);
    // Два коротких токена — только вектор (1 → 2), сами строки в куче не живут.
    EXPECT_EQ(tokens.size(), 2u);
    EXPECT_EQ(taskq::tools::allocations(), 2u);
}
