#include "file.hpp"

#include <string>

#include "errors.hpp"

FilePtr open_file(const std::filesystem::path& path, const char* mode) {
    FilePtr file(std::fopen(path.c_str(), mode));
    if (!file) {
        throw ChatError("cannot open file: " + path.string());
    }
    return file;
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    const FilePtr file = open_file(path, "rb");
    std::vector<std::string> lines;
    std::string current;
    int ch = 0;
    while ((ch = std::fgetc(file.get())) != EOF) {
        if (ch == '\n') {
            lines.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(static_cast<char>(ch));
        }
    }
    if (!current.empty()) {
        lines.push_back(std::move(current));
    }
    return lines;
}
