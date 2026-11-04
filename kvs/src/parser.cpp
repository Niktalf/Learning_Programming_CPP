#include "kvs/parser.hpp"

#include <charconv>

#include "kvs/lexer.hpp"

namespace kvs {
namespace {

[[nodiscard]] bool equals_ignore_case(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char lhs = (a[i] >= 'a' && a[i] <= 'z') ? static_cast<char>(a[i] - 32) : a[i];
        if (lhs != b[i]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::int64_t> to_int(std::string_view token) noexcept {
    std::int64_t value = 0;
    const char* const first = token.data();
    const char* const last = token.data() + token.size();
    // std::from_chars — из C++17: разбор без аллокаций, локали и исключений.
    const auto result = std::from_chars(first, last, value);
    // Проверка result.ptr != last существенна: без неё «12abc» разобралось бы
    // как 12, и мусор молча прошёл бы дальше.
    if (result.ec != std::errc{} || result.ptr != last) {
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
        // Шаг по умолчанию — единица; явно указанный обязан быть целым.
        const std::optional<std::int64_t> by =
            argc == 2 ? to_int(tokens[2]) : std::optional<std::int64_t>{1};
        if (!by) {
            return std::nullopt;
        }
        return IncrCommand{tokens[1], *by};
    }
    return std::nullopt;
}

}  // namespace kvs
