#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>

class AppendOnlyLog {
public:
    explicit AppendOnlyLog(const std::filesystem::path& path);

    AppendOnlyLog(const AppendOnlyLog&) = delete;
    AppendOnlyLog& operator=(const AppendOnlyLog&) = delete;
    AppendOnlyLog(AppendOnlyLog&&) noexcept = default;
    AppendOnlyLog& operator=(AppendOnlyLog&&) noexcept = default;
    ~AppendOnlyLog() = default;

    void append(std::string_view record);

private:
    std::ofstream out_;
};
