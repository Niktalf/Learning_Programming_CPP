#include "chat/lexer.hpp"

#include <string>

namespace chat {

Result<Tokens, ParseError> try_tokenize(std::string_view line, std::size_t max_tokens) {
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
                return Unexpected{ParseError("unterminated quote", std::string(line))};
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

    // Остаток строки целиком — текст сообщения.
    while (i < line.size() && line[i] == ' ') {
        ++i;
    }
    if (i < line.size()) {
        tokens.push_back(line.substr(i));
    }
    return tokens;
}

Tokens tokenize(std::string_view line, std::size_t max_tokens) {
    // value() бросает хранимую ошибку — ParseError — как есть.
    return try_tokenize(line, max_tokens).value();
}

}  // namespace chat
