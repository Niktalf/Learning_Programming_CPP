#include "lexer.hpp"

Tokens tokenize(const std::string_view line) {
    Tokens tokens;
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
            tokens.push_back(line.substr(i, end - i));
            i = end + 1;
        } else {
            const auto end = line.find(' ', i);
            const auto count = (end == std::string_view::npos) ? line.size() - i : end - i;
            tokens.push_back(line.substr(i, count));
            i += count;
        }
    }
    return tokens;
}
