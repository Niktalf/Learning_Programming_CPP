#include "taskq/parser.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>

#include "taskq/lexer.hpp"
#include "taskq/validate.hpp"

namespace taskq {
namespace {

// Сравнение без учёта регистра: submit и SUBMIT — одна команда. Образец всегда
// записан заглавными буквами.
[[nodiscard]] bool is(std::string_view token, std::string_view pattern) noexcept {
    if (token.size() != pattern.size()) {
        return false;
    }
    for (std::size_t i = 0; i < token.size(); ++i) {
        const char upper = (token[i] >= 'a' && token[i] <= 'z')
                               ? static_cast<char>(token[i] - 'a' + 'A')
                               : token[i];
        if (upper != pattern[i]) {
            return false;
        }
    }
    return true;
}

template <class T>
[[nodiscard]] std::optional<T> to_number(std::string_view token) noexcept {
    T value{};
    const char* const first = token.data();
    const char* const last = token.data() + token.size();
    // std::from_chars — из C++17: разбор без аллокаций, локали и исключений.
    const auto [end, error] = std::from_chars(first, last, value);
    // Проверка end != last существенна: без неё «12abc» разобралось бы как 12.
    if (error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

constexpr std::string_view priority_prefix = "prio=";
constexpr int max_priority = 9;

// Проверки, общие для обоих путей. Каждая возвращает причину отказа значением,
// а путь сам решает, бросить её или вернуть.

// Строка целиком: длина и кодировка проверяются до разбора.
[[nodiscard]] std::optional<std::string> line_problem(std::string_view line) {
    if (line.size() > max_line_length) {
        return "line too long: " + std::to_string(line.size()) + " bytes, limit is " +
               std::to_string(max_line_length);
    }
    if (!is_valid_utf8(line)) {
        return std::string("invalid UTF-8 (use bin: for binary payloads)");
    }
    return std::nullopt;
}

[[nodiscard]] bool is_priority(std::string_view token) noexcept {
    return token.substr(0, priority_prefix.size()) == priority_prefix;
}

// prio=N, где N от 0 до 9. Фаззер проверял prio=-1, prio=10 и prio=99999999999:
// все три отвергаются здесь, ещё на этапе 2.
[[nodiscard]] Result<int, std::string> priority_of(std::string_view token) {
    const std::string_view digits = token.substr(priority_prefix.size());
    const auto priority = to_number<int>(digits);
    if (!priority || *priority < 0 || *priority > max_priority) {
        return Unexpected{"priority must be 0.." + std::to_string(max_priority) + ": " +
                          std::string(digits)};
    }
    return *priority;
}

[[nodiscard]] Result<TaskId, std::string> id_of(std::string_view token) {
    if (const auto id = to_number<TaskId>(token); id) {
        return *id;
    }
    return Unexpected{"task id must be a non-negative number: " + std::string(token)};
}

[[nodiscard]] std::string arity(std::string_view verb, std::string_view expected) {
    return std::string(verb) + " expects " + std::string(expected);
}

// Разбор SUBMIT. Приоритет необязателен и задаётся именованным аргументом:
//
//     SUBMIT "build tasks" prio=5 make all
//     SUBMIT build make all                   приоритет 0
//
// Тело — весь остаток строки. Сколько токенов до хвоста, зависит от того, есть
// ли приоритет, поэтому строка разбирается дважды: сначала с хвостом после
// третьего токена, и если третий — не приоритет, то с хвостом после второго.
//
// Функция общая для обоих путей и возвращает Result: исключающий путь
// вызывает её через value().
[[nodiscard]] Result<SubmitCommand, ParseError> try_parse_submit(std::string_view line) {
    const auto reject = [line](std::string reason) {
        return Unexpected{ParseError(std::move(reason), std::string(line))};
    };

    const auto with_priority = try_tokenize(line, 4);
    if (!with_priority) {
        return Unexpected{with_priority.error()};
    }
    const Tokens& tokens = *with_priority;
    if (tokens.size() < 2) {
        return reject(arity("SUBMIT", "a queue and a body"));
    }
    if (const auto problem = queue_name_problem(tokens[1]); problem) {
        return reject(*problem);
    }
    if (tokens.size() >= 3 && is_priority(tokens[2])) {
        const auto priority = priority_of(tokens[2]);
        if (!priority) {
            return reject(priority.error());
        }
        // «SUBMIT build prio=5» без тела: приоритет есть, задачи нет.
        if (tokens.size() < 4) {
            return reject("SUBMIT needs a body");
        }
        return SubmitCommand{tokens[1], *priority, tokens[3]};
    }

    const auto plain = try_tokenize(line, 3);
    if (!plain) {
        return Unexpected{plain.error()};
    }
    if (plain->size() < 3) {
        return reject("SUBMIT needs a body");
    }
    return SubmitCommand{(*plain)[1], 0, (*plain)[2]};
}

// Разбор пакета: SUBMITBATCH очередь [prio=N] тело очередь [prio=N] тело ...
// Тело — один токен: несколько слов берутся в кавычки. Хвоста строки здесь
// нет, иначе было бы непонятно, где кончается одно тело и начинается другое.
[[nodiscard]] Result<BatchCommand, std::string> split_batch(const Tokens& tokens) {
    BatchCommand batch;
    std::size_t i = 1;
    while (i < tokens.size()) {
        const std::string_view queue = tokens[i++];
        if (const auto problem = queue_name_problem(queue); problem) {
            return Unexpected{*problem};
        }
        int priority = 0;
        if (i < tokens.size() && is_priority(tokens[i])) {
            const auto parsed = priority_of(tokens[i++]);
            if (!parsed) {
                return Unexpected{parsed.error()};
            }
            priority = *parsed;
        }
        if (i >= tokens.size()) {
            return Unexpected{"SUBMITBATCH item for " + std::string(queue) + " has no body"};
        }
        batch.items.push_back(SubmitCommand{queue, priority, tokens[i++]});
    }
    if (batch.items.empty()) {
        return Unexpected{std::string("SUBMITBATCH expects at least one item")};
    }
    return batch;
}

// Путь на исключениях. [[noreturn]] сообщает компилятору, что управление
// отсюда не вернётся, — тогда он не требует return после вызова.
[[noreturn]] void fail(std::string reason, std::string_view line) {
    throw ParseError(std::move(reason), std::string(line));
}

// Путь на Result: та же ошибка, но возвращается значением.
[[nodiscard]] Unexpected<ParseError> failure(std::string reason, std::string_view line) {
    return Unexpected{ParseError(std::move(reason), std::string(line))};
}

}  // namespace

// --- Путь первый: исключения ---------------------------------------------
//
// Успешный путь читается сверху вниз как прямой код. Ошибка лексера пролетает
// дальше сама: ни проверки, ни проброса вручную.
Command parse(const InputLine& input) {
    const std::string_view line = input.view();
    if (const auto problem = line_problem(line); problem) {
        fail(*problem, line);
    }
    const Tokens head = tokenize(line, 2);
    if (head.empty()) {
        fail("empty command", line);
    }
    const std::string_view verb = head.front();

    if (is(verb, "SUBMIT")) {
        return try_parse_submit(line).value();
    }

    const Tokens tokens = tokenize(line);
    const std::size_t argc = tokens.size() - 1;

    if (is(verb, "SUBMITBATCH")) {
        auto batch = split_batch(tokens);
        if (!batch) {
            fail(batch.error(), line);
        }
        return std::move(batch).value();
    }
    if (is(verb, "CREATE") || is(verb, "POLL")) {
        if (argc != 1) {
            fail(arity(verb, "a queue"), line);
        }
        if (const auto problem = queue_name_problem(tokens[1]); problem) {
            fail(*problem, line);
        }
        if (is(verb, "CREATE")) {
            return CreateCommand{tokens[1]};
        }
        return PollCommand{tokens[1]};
    }

    // Остальные команды адресуют задачу по номеру.
    const bool by_id = is(verb, "ACK") || is(verb, "FAIL") || is(verb, "RELEASE") ||
                       is(verb, "STATUS") || is(verb, "TYPE") || is(verb, "BUMP");
    if (!by_id) {
        fail("unknown command: " + std::string(verb), line);
    }
    if (argc == 0) {
        fail(arity(verb, "a task id"), line);
    }
    const auto id = id_of(tokens[1]);
    if (!id) {
        fail(id.error(), line);
    }

    if (is(verb, "BUMP")) {
        if (argc > 2) {
            fail(arity(verb, "a task id and an optional step"), line);
        }
        const auto by = argc == 2 ? to_number<std::int64_t>(tokens[2])
                                  : std::optional<std::int64_t>{1};
        if (!by) {
            fail("step must be a number: " + std::string(tokens[2]), line);
        }
        return BumpCommand{*id, *by};
    }
    if (argc != 1) {
        fail(arity(verb, "a task id"), line);
    }
    if (is(verb, "ACK")) {
        return AckCommand{*id};
    }
    if (is(verb, "FAIL")) {
        return FailCommand{*id};
    }
    if (is(verb, "RELEASE")) {
        return ReleaseCommand{*id};
    }
    if (is(verb, "STATUS")) {
        return StatusCommand{*id};
    }
    return TypeCommand{*id};
}

// --- Путь второй: ошибка-значение -----------------------------------------
//
// Тот же разбор, но ошибка возвращается. Каждый шаг, который может не
// удаться, проверяется на месте, и ошибка передаётся наверх вручную — в том
// числе ошибка лексера. Сравнение двух путей — в docs/errors.md.
Result<Command, ParseError> try_parse(const InputLine& input) {
    const std::string_view line = input.view();
    if (const auto problem = line_problem(line); problem) {
        return failure(*problem, line);
    }
    const auto head = try_tokenize(line, 2);
    if (!head) {
        return Unexpected{head.error()};
    }
    if (head->empty()) {
        return failure("empty command", line);
    }
    const std::string_view verb = head->front();

    if (is(verb, "SUBMIT")) {
        auto submit = try_parse_submit(line);
        if (!submit) {
            return Unexpected{submit.error()};
        }
        return Command{*submit};
    }

    const auto all = try_tokenize(line);
    if (!all) {
        return Unexpected{all.error()};
    }
    const Tokens& tokens = *all;
    const std::size_t argc = tokens.size() - 1;

    if (is(verb, "SUBMITBATCH")) {
        auto batch = split_batch(tokens);
        if (!batch) {
            return failure(batch.error(), line);
        }
        return Command{std::move(batch).value()};
    }
    if (is(verb, "CREATE") || is(verb, "POLL")) {
        if (argc != 1) {
            return failure(arity(verb, "a queue"), line);
        }
        if (const auto problem = queue_name_problem(tokens[1]); problem) {
            return failure(*problem, line);
        }
        if (is(verb, "CREATE")) {
            return Command{CreateCommand{tokens[1]}};
        }
        return Command{PollCommand{tokens[1]}};
    }

    const bool by_id = is(verb, "ACK") || is(verb, "FAIL") || is(verb, "RELEASE") ||
                       is(verb, "STATUS") || is(verb, "TYPE") || is(verb, "BUMP");
    if (!by_id) {
        return failure("unknown command: " + std::string(verb), line);
    }
    if (argc == 0) {
        return failure(arity(verb, "a task id"), line);
    }
    const auto id = id_of(tokens[1]);
    if (!id) {
        return failure(id.error(), line);
    }

    if (is(verb, "BUMP")) {
        if (argc > 2) {
            return failure(arity(verb, "a task id and an optional step"), line);
        }
        const auto by = argc == 2 ? to_number<std::int64_t>(tokens[2])
                                  : std::optional<std::int64_t>{1};
        if (!by) {
            return failure("step must be a number: " + std::string(tokens[2]), line);
        }
        return Command{BumpCommand{*id, *by}};
    }
    if (argc != 1) {
        return failure(arity(verb, "a task id"), line);
    }
    if (is(verb, "ACK")) {
        return Command{AckCommand{*id}};
    }
    if (is(verb, "FAIL")) {
        return Command{FailCommand{*id}};
    }
    if (is(verb, "RELEASE")) {
        return Command{ReleaseCommand{*id}};
    }
    if (is(verb, "STATUS")) {
        return Command{StatusCommand{*id}};
    }
    return Command{TypeCommand{*id}};
}

}  // namespace taskq
