#include "parser.hpp"

#include <charconv>
#include <cstddef>
#include <string>

#include "lexer.hpp"
#include "errors.hpp"

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

    [[noreturn]] void fail(std::string reason, const std::string_view line) {
        throw ParseError(std::move(reason), std::string(line));
    }

    template <class T>
    [[nodiscard]] T to_number(const std::string_view token, const std::string_view line) {
        T value{};
        const char* const first = token.data();
        const char* const last = token.data() + token.size();
        if (const auto result = std::from_chars(first, last, value); result.ec != std::errc{} || result.ptr != last) {
            fail("not a number: " + std::string(token), line);
        }
        return value;
    }

    [[nodiscard]] int to_priority(const std::string_view token, const std::string_view line) {
        const int priority = to_number<int>(token, line);
        if (priority < 0 || priority > 9) {
            fail("priority must be 0..9", line);
        }
        return priority;
    }

    [[nodiscard]] std::vector<std::string_view> split_queues(const std::string_view list) {
        std::vector<std::string_view> out;
        std::size_t start = 0;
        while (start <= list.size()) {
            const auto comma = list.find(',', start);
            if (const auto end = comma == std::string_view::npos ? list.size() : comma; end > start) {
                out.push_back(list.substr(start, end - start));
            }
            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1;
        }
        return out;
    }
}  // namespace

Command parse(const InputLine& line) {
    const std::string_view view = line.view();

    const Tokens head = tokenize(view, 2);
    if (head.empty()) {
        fail("empty command", view);
    }
    const bool has_body = is(head.front(), "SUBMIT") || is(head.front(), "BATCH");

    const Tokens tokens = has_body ? tokenize(view, 4) : tokenize(view);
    const std::string_view verb = tokens.front();
    const auto argc = tokens.size() - 1;

    const auto one_id = [&](const std::string_view what) {
        if (argc != 1) {
            fail(std::string(what) + " expects a task id", view);
        }
        return to_number<std::uint64_t>(tokens[1], view);
    };

    if (is(verb, "CREATE")) {
        if (argc != 1) fail("CREATE expects a queue", view);
        return CreateCommand{
            .queue = tokens[1]
        };
    }
    if (is(verb, "SUBMIT")) {
        if (argc != 3 || tokens[3].empty()) fail("SUBMIT expects queue priority body", view);
        return SubmitCommand{
            .queue = tokens[1],
            .priority = to_priority(tokens[2], view),
            .body = tokens[3]
        };
    }
    if (is(verb, "BATCH")) {
        if (argc != 3 || tokens[3].empty()) {
            fail("BATCH expects q1,q2,... priority body", view);
        }
        const int priority = to_priority(tokens[2], view);
        auto queues = split_queues(tokens[1]);
        if (queues.empty()) fail("BATCH expects at least one queue", view);
        return BatchCommand{
            .queues = std::move(queues),
            .priority = priority,
            .body = tokens[3]
        };
    }
    if (is(verb, "POLL")) {
        if (argc != 2) fail("POLL expects queue and worker", view);
        return PollCommand{
            .queue = tokens[1],
            .worker = tokens[2]
        };
    }
    if (is(verb, "WORKER")) {
        if (argc != 1) fail("WORKER expects a name", view);
        return WorkerCommand{
            .name = tokens[1]
        };
    }
    if (is(verb, "GONE")) {
        if (argc != 1) fail("GONE expects a name", view);
        return GoneCommand{
            .name = tokens[1]
        };
    }
    if (is(verb, "ACK")) {
        return AckCommand{
            .id = one_id("ACK")
        };
    }
    if (is(verb, "FAIL")) {
        return FailCommand{
            .id = one_id("FAIL")
        };
    }
    if (is(verb, "RELEASE")) {
        return ReleaseCommand{
            .id = one_id("RELEASE")
        };
    }
    if (is(verb, "STATUS")) {
        return StatusCommand{
            .id = one_id("STATUS")
        };
    }
    if (is(verb, "TYPE")) {
        return TypeCommand{
            .id = one_id("TYPE")
        };
    }
    if (is(verb, "QUEUES")) {
        if (argc != 0) fail("QUEUES takes no arguments", view);
        return QueuesCommand{};
    }
    if (is(verb, "BUMP")) {
        if (argc != 1 && argc != 2) fail("BUMP expects id [by]", view);
        const auto id = to_number<std::uint64_t>(tokens[1], view);
        const std::int64_t by = argc == 2 ? to_number<std::int64_t>(tokens[2], view) : 1;
        return BumpCommand{
            .id = id,
            .by = by
        };
    }
    fail("unknown command: " + std::string(verb), view);
}
