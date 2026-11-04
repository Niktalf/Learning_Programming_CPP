#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "kvs/errors.hpp"
#include "kvs/file.hpp"
#include "kvs/log.hpp"
#include "temp_dir.hpp"

using kvs::testing::open_descriptors;
using kvs::testing::TempDir;

namespace {

// Делетер-функция — для сравнения размеров. Не std::fclose напрямую: брать
// адрес функции стандартной библиотеки стандарт не разрешает.
void close_file(std::FILE* file) noexcept {
    if (file != nullptr) {
        std::fclose(file);
    }
}

}  // namespace

TEST(File, DeleterSize) {
    // Функтор без состояния и лямбда без захвата — пустые классы: unique_ptr
    // кладёт их как пустую базу, и размер равен размеру сырого указателя.
    EXPECT_EQ(sizeof(kvs::FilePtr), sizeof(std::FILE*));

    const auto closer = [](std::FILE* file) noexcept {
        if (file != nullptr) {
            std::fclose(file);
        }
    };
    EXPECT_EQ(sizeof(std::unique_ptr<std::FILE, decltype(closer)>), sizeof(std::FILE*));

    // Указатель на функцию — данные, его надо хранить рядом с указателем на
    // файл. Размер удваивается.
    using FunctionDeleter = std::unique_ptr<std::FILE, void (*)(std::FILE*) noexcept>;
    EXPECT_EQ(sizeof(FunctionDeleter), 2 * sizeof(std::FILE*));
    const FunctionDeleter unused(nullptr, close_file);
    static_cast<void>(unused);
}

TEST(File, DeleterClosesTheFile) {
    const TempDir dir;
    const auto path = dir.file("data.txt");
    {
        kvs::AppendOnlyLog log(path);
        log.append("line");
    }
    const int before = open_descriptors();
    {
        const kvs::FilePtr file = kvs::open_file(path, "rb");
        EXPECT_EQ(open_descriptors(), before + 1);
    }
    EXPECT_EQ(open_descriptors(), before);
}

TEST(File, ReadLines) {
    const TempDir dir;
    const auto path = dir.file("data.txt");
    {
        kvs::AppendOnlyLog log(path);
        log.append("first");
        log.append("");
        log.append("third with spaces");
    }
    EXPECT_EQ(kvs::read_lines(path),
              (std::vector<std::string>{"first", "", "third with spaces"}));
}

TEST(File, OpenFailureIsAnError) {
    const TempDir dir;
    EXPECT_THROW(static_cast<void>(kvs::open_file(dir.file("absent.txt"), "rb")), kvs::KvError);
}
