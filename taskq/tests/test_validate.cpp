// Проверки ввода, найденные фаззером (tests/test_fuzz.cpp). Каждая находка
// закрыта здесь отдельным тестом.

#include <gtest/gtest.h>

#include <exception>
#include <string>

#include "taskq/errors.hpp"
#include "taskq/service.hpp"
#include "taskq/validate.hpp"

namespace {

// С этапа 6 разбирает реестр; ошибку аргумента он оборачивает подсказкой,
// поэтому причина — по корню цепочки.
std::string reason_for(const std::string& text) {
    try {
        static_cast<void>(taskq::Service::commands().bind(text));
    } catch (...) {
        try {
            std::rethrow_exception(taskq::root_cause(std::current_exception()));
        } catch (const taskq::ParseError& e) {
            return e.reason();
        } catch (...) {
            return "not a parse error";
        }
    }
    return "parsed";
}

}  // namespace

TEST(Validate, Utf8) {
    EXPECT_TRUE(taskq::is_valid_utf8("make all"));
    EXPECT_TRUE(taskq::is_valid_utf8("собрать всё"));
    EXPECT_FALSE(taskq::is_valid_utf8("\xd0"));
    EXPECT_FALSE(taskq::is_valid_utf8("\xc0\xaf"));
    EXPECT_FALSE(taskq::is_valid_utf8("\xed\xa0\x80"));
    EXPECT_FALSE(taskq::is_valid_utf8("\xff"));
}

TEST(Validate, QueueNames) {
    EXPECT_FALSE(taskq::queue_name_problem("build").has_value());
    EXPECT_FALSE(taskq::queue_name_problem("build tasks").has_value());   // пробел внутри
    EXPECT_TRUE(taskq::queue_name_problem("   ").has_value());           // из одних пробелов
    EXPECT_TRUE(taskq::queue_name_problem(" build").has_value());
    EXPECT_TRUE(taskq::queue_name_problem("build\t").has_value());
    EXPECT_TRUE(taskq::queue_name_problem(std::string(65, 'q')).has_value());
}

TEST(Validate, ParserRejectsWhatTheFuzzerFound) {
    EXPECT_EQ(reason_for(R"(CREATE "   ")"), std::string("queue name is empty"));
    EXPECT_EQ(reason_for("SUBMIT build " + std::string(1 << 20, 'x')),
              std::string("line too long: 1048589 bytes, limit is 65536"));
    EXPECT_EQ(reason_for("SUBMIT build make\xd0"),
              std::string("invalid UTF-8 (use bin: for binary payloads)"));
    // Приоритет за пределами 0..9 отвергался и на этапе 2; фаззер это подтвердил.
    EXPECT_EQ(reason_for("SUBMIT build prio=-1 x"), std::string("priority must be 0..9: -1"));
    EXPECT_EQ(reason_for("SUBMIT build prio=99999999999 x"),
              std::string("priority must be 0..9: 99999999999"));
}
