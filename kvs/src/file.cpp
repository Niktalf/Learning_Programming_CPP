#include "kvs/file.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "kvs/errors.hpp"

namespace kvs {

FilePtr open_file(const std::filesystem::path& path, const char* mode) {
    FilePtr file(std::fopen(path.c_str(), mode));
    if (!file) {
        throw KvError("cannot open file: " + path.string());
    }
    return file;
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    const FilePtr file = open_file(path, "rb");
    std::vector<std::string> lines;
    std::string line;
    // Посимвольное чтение: getline из C — расширение POSIX, а построчное
    // fgets требует буфера заранее известного размера.
    for (int c = std::fgetc(file.get()); c != EOF; c = std::fgetc(file.get())) {
        if (c == '\n') {
            lines.push_back(std::move(line));
            line.clear();
        } else {
            line.push_back(static_cast<char>(c));
        }
    }
    if (std::ferror(file.get()) != 0) {
        throw KvError("cannot read file: " + path.string());
    }
    if (!line.empty()) {
        lines.push_back(std::move(line));   // последняя строка без перевода
    }
    return lines;
    // Файл закрывается здесь — и при возврате, и при любом броске выше.
}

}  // namespace kvs
