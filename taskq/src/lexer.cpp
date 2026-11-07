#include "taskq/lexer.hpp"

#include <string>

namespace taskq {
namespace {

[[nodiscard]] std::size_t skip_spaces(std::string_view line, std::size_t i) noexcept {
    while (i < line.size() && line[i] == ' ') {
        ++i;
    }
    return i;
}

// Один токен, начиная с позиции i (пробелы уже пропущены). Сдвигает i за
// токен. Единственная ошибка — незакрытая кавычка. Общая часть tokenize и
// TokenCursor: правила разбиения строки — в одном месте.
[[nodiscard]] std::expected<std::string_view, ParseError> scan(std::string_view line,
                                                               std::size_t& i) {
    if (line[i] == '"') {
        const auto end = line.find('"', ++i);
        if (end == std::string_view::npos) {
            return std::unexpected(ParseError("unterminated quote", std::string(line)));
        }
        const std::string_view token = line.substr(i, end - i);
        i = end + 1;
        return token;
    }
    const auto end = line.find(' ', i);
    const auto count = (end == std::string_view::npos) ? line.size() - i : end - i;
    const std::string_view token = line.substr(i, count);
    i += count;
    return token;
}

}  // namespace

std::expected<Tokens, ParseError> try_tokenize(std::string_view line, std::size_t max_tokens) {
    Tokens tokens;
    std::size_t i = skip_spaces(line, 0);
    while (i < line.size() && tokens.size() + 1 < max_tokens) {
        auto token = scan(line, i);
        if (!token) {
            return std::unexpected(std::move(token).error());
        }
        tokens.push_back(*token);
        i = skip_spaces(line, i);
    }
    // Остаток строки целиком — тело задачи.
    if (i < line.size()) {
        tokens.push_back(line.substr(i));
    }
    return tokens;
}

Tokens tokenize(std::string_view line, std::size_t max_tokens) {
    // Этап 5: вместо собственного Result — std::expected. value() у него
    // бросает не саму ошибку, а std::bad_expected_access<ParseError>, поэтому
    // ошибка бросается явно: граница по-прежнему получает ParseError.
    auto tokens = try_tokenize(line, max_tokens);
    if (!tokens) {
        throw std::move(tokens).error();
    }
    return *std::move(tokens);
}

std::optional<std::string_view> TokenCursor::peek() {
    std::size_t i = skip_spaces(line_, position_);
    if (i >= line_.size()) {
        return std::nullopt;
    }
    auto token = scan(line_, i);
    if (!token) {
        throw std::move(token).error();
    }
    return *token;
}

std::optional<std::string_view> TokenCursor::next() {
    position_ = skip_spaces(line_, position_);
    if (position_ >= line_.size()) {
        return std::nullopt;
    }
    auto token = scan(line_, position_);
    if (!token) {
        throw std::move(token).error();
    }
    return *token;
}

std::string_view TokenCursor::rest() noexcept {
    position_ = skip_spaces(line_, position_);
    const std::string_view remainder = line_.substr(position_);
    position_ = line_.size();
    return remainder;
}

}  // namespace taskq
