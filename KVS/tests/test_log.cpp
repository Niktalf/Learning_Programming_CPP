#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "errors.hpp"
#include "file.hpp"
#include "log.hpp"

namespace fs = std::filesystem;

namespace {
    fs::path temp_log(const char* name) {
        const fs::path path = fs::temp_directory_path() / name;
        fs::remove(path);
        return path;
    }
}  // namespace

TEST(Log, WritesAndClosesOnScopeExit) {
    const fs::path path = temp_log("kvs_log_scope.log");
    {
        AppendOnlyLog log(path);
        log.append("SET a 1");
        log.append("SET b 2");
    }
    const auto lines = read_lines(path);

    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[1], std::string("SET b 2"));

    fs::remove(path);
}

TEST(Log, ClosesEvenWhenLeavingThroughException) {
    const fs::path path = temp_log("kvs_log_throw.log");
    try {
        AppendOnlyLog log(path);
        log.append("before");
        throw std::runtime_error("что-то пошло не так");
    } catch (const std::runtime_error&) {}
    const auto lines = read_lines(path);

    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], std::string("before"));

    fs::remove(path);
}

TEST(Log, ConstructorThrowsWhenFileCannotBeOpened) {
    EXPECT_THROW(AppendOnlyLog("/nonexistent-directory/kvs.log"), KvError);
}

TEST(Log, IsMovableButNotCopyable) {
    EXPECT_FALSE(std::is_copy_constructible_v<AppendOnlyLog>);
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<AppendOnlyLog>);
}

TEST(Log, AppendsAcrossReopening) {
    const fs::path path = temp_log("kvs_log_append.log");

    { AppendOnlyLog log(path); log.append("first"); }
    { AppendOnlyLog log(path); log.append("second"); }

    EXPECT_EQ(read_lines(path).size(), 2u);

    fs::remove(path);
}
