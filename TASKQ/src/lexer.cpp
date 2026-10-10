#include "lexer.hpp"

#include <string>

#include "errors.hpp"

Tokens tokenize(const std::string_view line, const std::size_t max_tokens) {
    Tokens tokens;
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
                throw ParseError("unterminated quote", std::string(line));
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

    while (i < line.size() && line[i] == ' ') {
        ++i;
    }
    if (i < line.size()) {
        tokens.push_back(line.substr(i));
    }
    return tokens;
}

