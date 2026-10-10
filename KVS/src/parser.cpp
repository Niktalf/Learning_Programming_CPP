#include "parser.hpp"

#include <charconv>
#include <optional>
#include <string>

#include "errors.hpp"
#include "lexer.hpp"

namespace {
    [[nodiscard]] bool equals_ignore_case(std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (const char lhs = a[i] >= 'a' && a[i] <= 'z' ? static_cast<char>(a[i] - 32) : a[i]; lhs != b[i]) {
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

    [[noreturn]] void fail(std::string reason, const std::string_view line) {
        throw ParseError(std::move(reason), std::string(line));
    }
}  // namespace

Command parse(const InputLine& line) {
    const std::string_view view = line.view();

    const Tokens tokens = tokenize(view);
    if (tokens.empty()) {
        fail("empty command", view);
    }

    const std::string_view verb = tokens.front();
    const auto argc = tokens.size() - 1;

    if (equals_ignore_case(verb, "SET")) {
        if (argc != 2) fail("SET expects 2 arguments", view);
        return SetCommand {
            .key = tokens[1],
            .value = tokens[2]
        };
    }
    if (equals_ignore_case(verb, "GET")) {
        if (argc != 1) fail("GET expects 1 argument", view);
        return GetCommand {
            .key = tokens[1]
        };
    }
    if (equals_ignore_case(verb, "DEL")) {
        if (argc != 1) fail("DEL expects 1 argument", view);
        return DelCommand {
            .key = tokens[1]
        };
    }
    if (equals_ignore_case(verb, "EXISTS")) {
        if (argc != 1) fail("EXISTS expects 1 argument", view);
        return ExistsCommand {
            .key = tokens[1]
        };
    }
    if (equals_ignore_case(verb, "TYPE")) {
        if (argc != 1) fail("TYPE expects 1 argument", view);
        return TypeCommand {
            .key = tokens[1]
        };
    }
    if (equals_ignore_case(verb, "INCR")) {
        if (argc != 1 && argc != 2) fail("INCR expects 1 or 2 arguments", view);
        std::int64_t by = 1;
        if (argc == 2) {
            const auto parsed = to_int(tokens[2]);
            if (!parsed) fail("not an integer: " + std::string(tokens[2]), view);
            by = *parsed;
        }
        return IncrCommand {
            .key = tokens[1],
            .by = by
        };
    }
    if (equals_ignore_case(verb, "MSET")) {
        if (argc == 0 || argc % 2 != 0) {
            fail("MSET expects key-value pairs", view);
        }
        MsetCommand mset;
        mset.pairs.reserve(argc / 2);
        for (std::size_t i = 1; i < tokens.size(); i += 2) {
            mset.pairs.emplace_back(tokens[i], tokens[i + 1]);
        }
        return mset;
    }
    if (equals_ignore_case(verb, "RPUSH")) {
        if (argc != 2) fail("RPUSH expects key and item", view);
        return RPushCommand {
            .key = tokens[1], .item = tokens[2]
        };
    }
    if (equals_ignore_case(verb, "LLEN")) {
        if (argc != 1) fail("LLEN expects 1 argument", view);
        return LLenCommand {
            .key = tokens[1]
        };
    }
    fail("unknown command: " + std::string(verb), view);
}
