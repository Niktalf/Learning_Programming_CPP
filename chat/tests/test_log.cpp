#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "chat/errors.hpp"
#include "chat/log.hpp"
#include "temp_dir.hpp"

using chat::AppendOnlyLog;
using chat::testing::open_descriptors;
using chat::testing::TempDir;

namespace {

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST(Log, OwnershipRules) {
    // Копировать нельзя — два владельца одного файла. Перемещать можно.
    EXPECT_FALSE(std::is_copy_constructible_v<AppendOnlyLog>);
    EXPECT_FALSE(std::is_copy_assignable_v<AppendOnlyLog>);
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<AppendOnlyLog>);
}

TEST(Log, AppendsRecordsInOrder) {
    const TempDir dir;
    const auto path = dir.file("journal.log");
    {
        AppendOnlyLog log(path);
        log.append("#a\talice\thi");
        log.append("#b\tbob\they");
    }
    {
        AppendOnlyLog log(path);   // дозапись, а не перезапись
        log.append("#a\tcarol\tbye");
    }
    EXPECT_EQ(read_lines(path), (std::vector<std::string>{"#a\talice\thi", "#b\tbob\they", "#a\tcarol\tbye"}));
}

TEST(Log, ClosedWhenScopeEnds) {
    const TempDir dir;
    const int before = open_descriptors();
    {
        AppendOnlyLog log(dir.file("journal.log"));
        EXPECT_EQ(open_descriptors(), before + 1);
    }
    EXPECT_EQ(open_descriptors(), before);
}

TEST(Log, ClosedOnException) {
    // Главный тест этапа: выход из области видимости через исключение тоже
    // закрывает файл. close в интерфейсе нет — закрывает деструктор, а
    // деструктор вызывает раскрутка стека.
    const TempDir dir;
    const auto path = dir.file("journal.log");
    const int before = open_descriptors();
    try {
        AppendOnlyLog log(path);
        log.append("#a\talice\thi");
        EXPECT_EQ(open_descriptors(), before + 1);
        throw std::runtime_error("something failed mid-operation");
    } catch (const std::runtime_error&) {
        // исключение поймано уже после разрушения log
    }
    EXPECT_EQ(open_descriptors(), before);
    // И записанное до броска не потеряно: append сбрасывает буфер сразу.
    EXPECT_EQ(read_lines(path), (std::vector<std::string>{"#a\talice\thi"}));
}

TEST(Log, ConstructorThrowsWhenFileCannotBeOpened) {
    const TempDir dir;
    const int before = open_descriptors();
    // Каталога нет — файл не откроется. Объект не построен, деструктор не
    // вызовется, но поле-поток разрушится само, и дескриптор не утечёт.
    EXPECT_THROW(AppendOnlyLog(dir.file("missing") / "journal.log"), chat::ChatError);
    EXPECT_EQ(open_descriptors(), before);
}

TEST(Log, MoveTransfersTheFile) {
    const TempDir dir;
    const auto path = dir.file("journal.log");
    const int before = open_descriptors();
    {
        AppendOnlyLog first(path);
        AppendOnlyLog second(std::move(first));
        second.append("#a\talice\thi");
        // Файл один, владелец один — второй объект.
        EXPECT_EQ(open_descriptors(), before + 1);
    }
    EXPECT_EQ(open_descriptors(), before);
    EXPECT_EQ(read_lines(path), (std::vector<std::string>{"#a\talice\thi"}));
}

TEST(Log, WriteFailureIsAnError) {
    // /dev/full принимает открытие и отказывает в каждой записи.
    if (!std::filesystem::exists("/dev/full")) {
        GTEST_SKIP() << "/dev/full is not available";
    }
    AppendOnlyLog log("/dev/full");
    EXPECT_THROW(log.append("#a\talice\thi"), chat::ChatError);
}
