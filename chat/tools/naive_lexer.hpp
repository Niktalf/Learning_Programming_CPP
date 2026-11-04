#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace chat::tools {

// Лексер «как на этапе 1»: те же правила, что у chat::tokenize, включая хвост
// строки после max_tokens - 1 токенов, но каждый токен копируется в
// собственный std::string. Нужен только звёздочке этапа 2.
inline std::vector<std::string> tokenize_copying(
    std::string_view line,
    std::size_t max_tokens = (std::numeric_limits<std::size_t>::max)()) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < line.size() && tokens.size() + 1 < max_tokens) {
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
    while (i < line.size() && line[i] == ' ') {
        ++i;
    }
    if (i < line.size()) {
        tokens.emplace_back(line.substr(i));
    }
    return tokens;
}

}  // namespace chat::tools
