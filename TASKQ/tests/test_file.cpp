#include <gtest/gtest.h>

#include <cstdio>
#include <memory>

#include "errors.hpp"
#include "file.hpp"

namespace {
    void close_file(std::FILE* file) noexcept {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
}  // namespace

TEST(File, StatelessDeleterAddsNoSize) {
    EXPECT_EQ(sizeof(FilePtr), sizeof(std::FILE*));

    auto closer = [](std::FILE* f) noexcept { if (f) std::fclose(f); };
    using LambdaPtr = std::unique_ptr<std::FILE, decltype(closer)>;

    EXPECT_EQ(sizeof(LambdaPtr), sizeof(std::FILE*));
}

TEST(File, FunctionPointerDeleterDoublesSize) {
    using FnPtr = std::unique_ptr<std::FILE, void (*)(std::FILE*) noexcept>;
    const FnPtr file(nullptr, &close_file);

    EXPECT_EQ(sizeof(FnPtr), 2 * sizeof(std::FILE*));
}

TEST(File, OpenFailureThrows) {
    EXPECT_THROW((void)open_file("/nonexistent-directory/x", "rb"), TaskQError);
}
