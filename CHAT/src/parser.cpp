#include "parser.hpp"

#include <charconv>
#include <cstddef>

#include "lexer.hpp"

namespace {
[[nodiscard]] bool is(const std::string_view token, const std::string_view name) noexcept {
    if (token.size() != name.size()) {
        return false;
    }
    for (std::size_t i = 0; i < token.size(); ++i) {
        const char c = (token[i] >= 'A' && token[i] <= 'Z')
                           ? static_cast<char>(token[i] + 32)
                           : token[i];
        if (c != name[i]) {
            return false;
        }
    }
    return true;
}

template <class T>
[[nodiscard]] std::optional<T> to_number(const std::string_view token) noexcept {
    T value{};
    const char* const first = token.data();
    const char* const last = token.data() + token.size();

    if (const auto result = std::from_chars(first, last, value); result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool is_room(const std::string_view token) noexcept {
    return !token.empty() && token.front() == '#';
}

}  // namespace

std::optional<Command> parse(const InputLine& line) {
    const Tokens head = tokenize(line.view(), 2);
    if (head.empty()) {
        return std::nullopt;
    }

    const Tokens tokens =
        is(head.front(), "/msg") ? tokenize(line.view(), 3) : tokenize(line.view());
    if (tokens.empty()) {
        return std::nullopt;
    }

    const std::string_view verb = tokens.front();
    const auto argc = tokens.size() - 1;

    if (is(verb, "/join") && argc == 1 && is_room(tokens[1])) {
        return JoinCommand{tokens[1]};
    }
    if (is(verb, "/leave") && argc == 1 && is_room(tokens[1])) {
        return LeaveCommand{tokens[1]};
    }
    if (is(verb, "/msg") && argc == 2 && is_room(tokens[1]) && !tokens[2].empty()) {
        return MsgCommand{tokens[1], tokens[2]};
    }
    if (is(verb, "/rooms") && argc == 0) {
        return RoomsCommand{};
    }
    if (is(verb, "/history") && (argc == 1 || argc == 2) && is_room(tokens[1])) {
        std::size_t limit = 20;
        if (argc == 2) {
            if (const auto parsed = to_number<std::size_t>(tokens[2]); parsed) {
                limit = *parsed;
            } else {
                return std::nullopt;
            }
        }
        return HistoryCommand{tokens[1], limit};
    }
    if (is(verb, "/type") && argc == 2 && is_room(tokens[1])) {
        if (const auto index = to_number<std::size_t>(tokens[2]); index) {
            return TypeCommand{tokens[1], *index};
        }
        return std::nullopt;
    }
    if (is(verb, "/react") && (argc == 2 || argc == 3) && is_room(tokens[1])) {
        const auto index = to_number<std::size_t>(tokens[2]);
        if (!index) {
            return std::nullopt;
        }
        std::int64_t by = 1;
        if (argc == 3) {
            if (const auto parsed = to_number<std::int64_t>(tokens[3]); parsed) {
                by = *parsed;
            } else {
                return std::nullopt;
            }
        }
        return ReactCommand{tokens[1], *index, by};
    }
    return std::nullopt;
}
