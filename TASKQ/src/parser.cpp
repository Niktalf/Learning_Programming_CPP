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
        const char c = (token[i] >= 'a' && token[i] <= 'z')
                           ? static_cast<char>(token[i] - 32)
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

}  // namespace

std::optional<Command> parse(const InputLine& line) {
    const Tokens head = tokenize(line.view(), 2);
    if (head.empty()) {
        return std::nullopt;
    }

    const Tokens tokens = is(head.front(), "SUBMIT") ? tokenize(line.view(), 4) : tokenize(line.view());
    if (tokens.empty()) {
        return std::nullopt;
    }

    const std::string_view verb = tokens.front();
    const auto argc = tokens.size() - 1;

    if (is(verb, "SUBMIT") && argc == 3) {
        const auto priority = to_number<int>(tokens[2]);
        if (!priority || *priority < 0 || *priority > 9 || tokens[3].empty()) {
            return std::nullopt;
        }
        return SubmitCommand{tokens[1], *priority, tokens[3]};
    }
    if (is(verb, "POLL") && argc == 1) {
        return PollCommand{tokens[1]};
    }
    if (is(verb, "ACK") && argc == 1) {
        return AckCommand{tokens[1]};
    }
    if (is(verb, "STATUS") && argc == 1) {
        return StatusCommand{tokens[1]};
    }
    if (is(verb, "QUEUES") && argc == 0) {
        return QueuesCommand{};
    }
    if (is(verb, "TYPE") && argc == 1) {
        return TypeCommand{tokens[1]};
    }
    if (is(verb, "BUMP") && (argc == 1 || argc == 2)) {
        std::int64_t by = 1;
        if (argc == 2) {
            if (const auto parsed = to_number<std::int64_t>(tokens[2]); parsed) {
                by = *parsed;
            } else {
                return std::nullopt;
            }
        }
        return BumpCommand{tokens[1], by};
    }
    return std::nullopt;
}
