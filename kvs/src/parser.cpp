#include "kvs/parser.hpp"

#include <charconv>
#include <optional>
#include <string>

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
    const auto result = std::from_chars(first, last, value);
    // Проверка result.ptr != last существенна: без неё «12abc» разобралось бы
    // как 12, и мусор молча прошёл бы дальше.
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    return value;
}

// Вспомогательная функция делает код ниже короче: ошибка разбора всегда
// несёт исходную строку, и собирать её в каждой ветке было бы шумно.
// [[noreturn]] сообщает компилятору, что управление отсюда не вернётся, —
// тогда он не требует return после вызова.
[[nodiscard]] std::optional<std::size_t> to_size(std::string_view token) noexcept {
    std::size_t value = 0;
    const char* const last = token.data() + token.size();
    const auto [end, error] = std::from_chars(token.data(), last, value);
    if (error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

[[noreturn]] void fail(std::string reason, std::string_view line) {
    throw ParseError(std::move(reason), std::string(line));
}

// То же для второго пути: ошибка не бросается, а возвращается значением.
[[nodiscard]] std::unexpected<ParseError> failure(std::string reason, std::string_view line) {
    return std::unexpected(ParseError(std::move(reason), std::string(line)));
}

}  // namespace

Command parse(const InputLine& input) {
    const std::string_view line = input.view();

    // Ошибка лексера пролетает дальше сама: ни проверки, ни проброса вручную.
    const Tokens tokens = tokenize(line);
    if (tokens.empty()) {
        fail("empty command", line);
    }

    const std::string_view verb = tokens.front();
    const auto argc = tokens.size() - 1;

    if (equals_ignore_case(verb, "SET")) {
        if (argc != 2) {
            fail("SET expects 2 arguments", line);
        }
        return SetCommand{tokens[1], tokens[2]};
    }
    if (equals_ignore_case(verb, "GET")) {
        if (argc != 1) {
            fail("GET expects 1 argument", line);
        }
        return GetCommand{tokens[1]};
    }
    if (equals_ignore_case(verb, "DEL")) {
        if (argc != 1) {
            fail("DEL expects 1 argument", line);
        }
        return DelCommand{tokens[1]};
    }
    if (equals_ignore_case(verb, "EXISTS")) {
        if (argc != 1) {
            fail("EXISTS expects 1 argument", line);
        }
        return ExistsCommand{tokens[1]};
    }
    if (equals_ignore_case(verb, "TYPE")) {
        if (argc != 1) {
            fail("TYPE expects 1 argument", line);
        }
        return TypeCommand{tokens[1]};
    }
    if (equals_ignore_case(verb, "INCR")) {
        if (argc != 1 && argc != 2) {
            fail("INCR expects 1 or 2 arguments", line);
        }
        // Шаг по умолчанию — единица; явно указанный обязан быть целым.
        const std::optional<std::int64_t> by =
            argc == 2 ? to_int(tokens[2]) : std::optional<std::int64_t>{1};
        if (!by) {
            fail("not an integer: " + std::string(tokens[2]), line);
        }
        return IncrCommand{tokens[1], *by};
    }
    if (equals_ignore_case(verb, "MSET")) {
        if (argc == 0 || argc % 2 != 0) {
            fail("MSET expects key-value pairs", line);
        }
        MsetCommand mset;
        mset.pairs.reserve(argc / 2);
        for (std::size_t i = 1; i < tokens.size(); i += 2) {
            mset.pairs.emplace_back(tokens[i], tokens[i + 1]);
        }
        return mset;
    }
    if (equals_ignore_case(verb, "RPUSH")) {
        if (argc != 2) {
            fail("RPUSH expects 2 arguments", line);
        }
        return RpushCommand{tokens[1], tokens[2]};
    }
    if (equals_ignore_case(verb, "LLEN")) {
        if (argc != 1) {
            fail("LLEN expects 1 argument", line);
        }
        return LlenCommand{tokens[1]};
    }
    if (equals_ignore_case(verb, "KEYS")) {
        if (argc > 2) {
            fail("KEYS expects an optional pattern and limit", line);
        }
        const std::string_view pattern = argc >= 1 ? tokens[1] : std::string_view("*");
        const std::optional<std::size_t> limit =
            argc == 2 ? to_size(tokens[2]) : std::optional<std::size_t>{default_keys_limit};
        if (!limit) {
            fail("limit must be a non-negative number: " + std::string(tokens[2]), line);
        }
        return KeysCommand{pattern, *limit};
    }
    if (equals_ignore_case(verb, "VERSION")) {
        if (argc != 1) {
            fail("VERSION expects 1 argument", line);
        }
        return VersionCommand{tokens[1]};
    }
    fail("unknown command: " + std::string(verb), line);
}

// Второй путь — та же логика разбора, но ошибка возвращается значением.
// Функция продублирована целиком, а не выражена через parse с перехватом:
// так видна настоящая цена этого стиля. Каждая проверка заканчивается явным
// return, и каждую ошибку снизу — здесь от лексера — надо проверить и
// передать наверх вручную. Сравнение двух путей — в docs/errors.md.
std::expected<Command, ParseError> try_parse(const InputLine& input) {
    const std::string_view line = input.view();

    auto tokenized = try_tokenize(line);
    if (!tokenized) {
        return std::unexpected(tokenized.error());   // ручной проброс ошибки лексера
    }
    const Tokens& tokens = *tokenized;
    if (tokens.empty()) {
        return failure("empty command", line);
    }

    const std::string_view verb = tokens.front();
    const auto argc = tokens.size() - 1;

    if (equals_ignore_case(verb, "SET")) {
        if (argc != 2) {
            return failure("SET expects 2 arguments", line);
        }
        return Command{SetCommand{tokens[1], tokens[2]}};
    }
    if (equals_ignore_case(verb, "GET")) {
        if (argc != 1) {
            return failure("GET expects 1 argument", line);
        }
        return Command{GetCommand{tokens[1]}};
    }
    if (equals_ignore_case(verb, "DEL")) {
        if (argc != 1) {
            return failure("DEL expects 1 argument", line);
        }
        return Command{DelCommand{tokens[1]}};
    }
    if (equals_ignore_case(verb, "EXISTS")) {
        if (argc != 1) {
            return failure("EXISTS expects 1 argument", line);
        }
        return Command{ExistsCommand{tokens[1]}};
    }
    if (equals_ignore_case(verb, "TYPE")) {
        if (argc != 1) {
            return failure("TYPE expects 1 argument", line);
        }
        return Command{TypeCommand{tokens[1]}};
    }
    if (equals_ignore_case(verb, "INCR")) {
        if (argc != 1 && argc != 2) {
            return failure("INCR expects 1 or 2 arguments", line);
        }
        const std::optional<std::int64_t> by =
            argc == 2 ? to_int(tokens[2]) : std::optional<std::int64_t>{1};
        if (!by) {
            return failure("not an integer: " + std::string(tokens[2]), line);
        }
        return Command{IncrCommand{tokens[1], *by}};
    }
    if (equals_ignore_case(verb, "MSET")) {
        if (argc == 0 || argc % 2 != 0) {
            return failure("MSET expects key-value pairs", line);
        }
        MsetCommand mset;
        mset.pairs.reserve(argc / 2);
        for (std::size_t i = 1; i < tokens.size(); i += 2) {
            mset.pairs.emplace_back(tokens[i], tokens[i + 1]);
        }
        return Command{std::move(mset)};
    }
    if (equals_ignore_case(verb, "RPUSH")) {
        if (argc != 2) {
            return failure("RPUSH expects 2 arguments", line);
        }
        return Command{RpushCommand{tokens[1], tokens[2]}};
    }
    if (equals_ignore_case(verb, "LLEN")) {
        if (argc != 1) {
            return failure("LLEN expects 1 argument", line);
        }
        return Command{LlenCommand{tokens[1]}};
    }
    if (equals_ignore_case(verb, "KEYS")) {
        if (argc > 2) {
            return failure("KEYS expects an optional pattern and limit", line);
        }
        const std::string_view pattern = argc >= 1 ? tokens[1] : std::string_view("*");
        const std::optional<std::size_t> limit =
            argc == 2 ? to_size(tokens[2]) : std::optional<std::size_t>{default_keys_limit};
        if (!limit) {
            return failure("limit must be a non-negative number: " + std::string(tokens[2]), line);
        }
        return Command{KeysCommand{pattern, *limit}};
    }
    if (equals_ignore_case(verb, "VERSION")) {
        if (argc != 1) {
            return failure("VERSION expects 1 argument", line);
        }
        return Command{VersionCommand{tokens[1]}};
    }
    return failure("unknown command: " + std::string(verb), line);
}

}  // namespace kvs
