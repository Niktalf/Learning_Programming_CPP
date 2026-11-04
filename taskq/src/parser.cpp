#include "taskq/parser.hpp"

#include <charconv>
#include <cstdint>

#include "taskq/lexer.hpp"

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

// Разбор SUBMIT. Приоритет необязателен и задаётся именованным аргументом:
//
//     SUBMIT "build tasks" prio=5 make all
//     SUBMIT build make all                   приоритет 0
//
// Тело — весь остаток строки. Сколько токенов до хвоста, зависит от того, есть
// ли приоритет, поэтому строка разбирается дважды: сначала с хвостом после
// третьего токена, и если третий — не приоритет, то с хвостом после второго.
// Второй проход дёшев: ни одной аллокации сверх вектора.
[[nodiscard]] std::optional<Command> parse_submit(std::string_view line) {
    const Tokens with_priority = tokenize(line, 4);
    if (with_priority.size() == 4 && with_priority[2].substr(0, priority_prefix.size()) ==
                                         priority_prefix) {
        const auto priority = to_number<int>(with_priority[2].substr(priority_prefix.size()));
        if (!priority || *priority < 0 || *priority > max_priority) {
            return std::nullopt;
        }
        return SubmitCommand{with_priority[1], *priority, with_priority[3]};
    }
    const Tokens plain = tokenize(line, 3);
    // «SUBMIT build prio=5» без тела: приоритет есть, задачи нет.
    if (plain.size() != 3 || plain[1].empty() ||
        plain[2].substr(0, priority_prefix.size()) == priority_prefix) {
        return std::nullopt;
    }
    return SubmitCommand{plain[1], 0, plain[2]};
}

}  // namespace

std::optional<Command> parse(const InputLine& line) {
    const Tokens head = tokenize(line.view(), 2);
    if (head.empty()) {
        return std::nullopt;
    }
    if (is(head.front(), "SUBMIT")) {
        return parse_submit(line.view());
    }

    const Tokens tokens = tokenize(line.view());
    if (tokens.size() < 2) {
        return std::nullopt;   // у всех остальных команд есть хотя бы один аргумент
    }
    const std::string_view verb = tokens.front();
    const std::size_t argc = tokens.size() - 1;

    if (is(verb, "POLL") && argc == 1) {
        return PollCommand{tokens[1]};
    }

    // Остальные команды адресуют задачу по номеру.
    const auto id = to_number<TaskId>(tokens[1]);
    if (!id) {
        return std::nullopt;
    }
    if (is(verb, "ACK") && argc == 1) {
        return AckCommand{*id};
    }
    if (is(verb, "FAIL") && argc == 1) {
        return FailCommand{*id};
    }
    if (is(verb, "STATUS") && argc == 1) {
        return StatusCommand{*id};
    }
    if (is(verb, "TYPE") && argc == 1) {
        return TypeCommand{*id};
    }
    if (is(verb, "BUMP") && (argc == 1 || argc == 2)) {
        const std::optional<std::int64_t> by =
            argc == 2 ? to_number<std::int64_t>(tokens[2]) : std::optional<std::int64_t>{1};
        if (!by) {
            return std::nullopt;
        }
        return BumpCommand{*id, *by};
    }
    return std::nullopt;
}

}  // namespace taskq
