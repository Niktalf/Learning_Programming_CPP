#include "parser.hpp"

#include <charconv>

#include "lexer.hpp"

namespace {

[[nodiscard]] bool equals_ignore_case(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (const char lhs = (a[i] >= 'a' && a[i] <= 'z') ? static_cast<char>(a[i] - 32) : a[i]; lhs != b[i]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::int64_t> to_int(const std::string_view token) noexcept {
    std::int64_t value = 0;
    const char* const first = token.data();
    const char* const last = token.data() + token.size();

    if (const auto [ptr, ec] = std::from_chars(first, last, value); ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::optional<Command> parse(const InputLine& line) {
    const Tokens tokens = tokenize(line.view());
    if (tokens.empty()) {
        return std::nullopt;
    }

    const std::string_view verb = tokens.front();
    const auto argc = tokens.size() - 1;

    if (equals_ignore_case(verb, "SET") && argc == 2) {
        return SetCommand{tokens[1], tokens[2]};
    }
    if (equals_ignore_case(verb, "GET") && argc == 1) {
        return GetCommand{tokens[1]};
    }
    if (equals_ignore_case(verb, "DEL") && argc == 1) {
        return DelCommand{tokens[1]};
    }
    if (equals_ignore_case(verb, "EXISTS") && argc == 1) {
        return ExistsCommand{tokens[1]};
    }
    if (equals_ignore_case(verb, "TYPE") && argc == 1) {
        return TypeCommand{tokens[1]};
    }
    if (equals_ignore_case(verb, "INCR") && (argc == 1 || argc == 2)) {
        std::int64_t by = 1;
        if (argc == 2) {
            // if с инициализацией: переменная не утекает за пределы проверки.
            if (const auto parsed = to_int(tokens[2]); parsed) {
                by = *parsed;
            } else {
                return std::nullopt;
            }
        }
        return IncrCommand{tokens[1], by};
    }
    return std::nullopt;
}