#include "kvs/lexer.hpp"

#include <string>
#include <utility>

namespace kvs {

Result<Tokens, ParseError> try_tokenize(std::string_view line) {
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
    return tokens;
}

Tokens tokenize(std::string_view line) {
    // value() бросает хранимую ошибку — ParseError — как есть.
    return try_tokenize(line).value();
}

}  // namespace kvs
