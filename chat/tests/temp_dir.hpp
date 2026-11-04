#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace chat::testing {

// Временный каталог на время теста — RAII в самих тестах: удаляется в
// деструкторе, даже если проверка упала посреди теста.
class TempDir {
public:
    TempDir() : path_(std::filesystem::temp_directory_path() / unique_name()) {
        std::filesystem::create_directories(path_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir() {
        std::error_code ignored;   // деструктор не бросает
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] std::filesystem::path file(const std::string& name) const { return path_ / name; }

private:
    static std::string unique_name() {
        std::random_device seed;
        return "chat-test-" + std::to_string(seed()) + std::to_string(seed());
    }
    std::filesystem::path path_;
};

// Число открытых дескрипторов процесса — по содержимому /proc/self/fd.
// Им доказывается, что журнал закрыл файл: просто «тест не упал» этого не
// показывает.
inline int open_descriptors() {
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        static_cast<void>(entry);
        ++count;
    }
    return count;
}

}  // namespace chat::testing
