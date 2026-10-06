#pragma once

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct FileCloser {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

[[nodiscard]] FilePtr open_file(const std::filesystem::path& path, const char* mode);

[[nodiscard]] std::vector<std::string> read_lines(const std::filesystem::path& path);
