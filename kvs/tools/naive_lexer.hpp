#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kvs::tools {

// Лексер «как на этапе 1»: те же правила, что у kvs::tokenize, но каждый токен
// копируется в собственный std::string. Нужен только звёздочке этапа 2 — для
// сравнения числа аллокаций.
inline std::vector<std::string> tokenize_copying(std::string_view line) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }
        if (line[i] == '"') {
            const auto end = line.find('"', ++i);
            if (end == std::string_view::npos) {
                return {};
            }
            tokens.emplace_back(line.substr(i, end - i));
            i = end + 1;
        } else {
            const auto end = line.find(' ', i);
            const auto count = (end == std::string_view::npos) ? line.size() - i : end - i;
            tokens.emplace_back(line.substr(i, count));
            i += count;
        }
    }
    return tokens;
}

}  // namespace kvs::tools
