#include "kvs/lexer.hpp"

#include <string>
#include <utility>

namespace kvs {

std::expected<Tokens, ParseError> try_tokenize(std::string_view line) {
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
                return std::unexpected(ParseError("unterminated quote", std::string(line)));
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
    // Этап 5: вместо собственного Result — std::expected. Одно отличие
    // существенно: value() у std::expected бросает не саму ошибку, а
    // std::bad_expected_access<ParseError>. Поэтому ошибка бросается явно,
    // чтобы граница по-прежнему получала ParseError с причиной.
    auto tokens = try_tokenize(line);
    if (!tokens) {
        throw std::move(tokens).error();
    }
    return *std::move(tokens);
}

}  // namespace kvs
