#include "chat/parser.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>

#include "chat/lexer.hpp"
#include "chat/validate.hpp"

namespace chat {
namespace {

// Сравнение без учёта регистра: /JOIN и /join — одна команда. Образец всегда
// записан строчными буквами.
[[nodiscard]] bool is(std::string_view token, std::string_view pattern) noexcept {
    if (token.size() != pattern.size()) {
        return false;
    }
    for (std::size_t i = 0; i < token.size(); ++i) {
        const char lower = (token[i] >= 'A' && token[i] <= 'Z')
                               ? static_cast<char>(token[i] - 'A' + 'a')
                               : token[i];
        if (lower != pattern[i]) {
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

// Проверки, общие для обоих путей. Каждая возвращает причину отказа, а путь
// сам решает, бросить её или вернуть.

// Строка целиком: длина и кодировка проверяются до разбора. Фаззер показал,
// что без этого в историю ложились и мегабайтные строки, и обрезанный UTF-8.
[[nodiscard]] std::optional<std::string> line_problem(std::string_view line) {
    if (line.size() > max_line_length) {
        return "line too long: " + std::to_string(line.size()) + " bytes, limit is " +
               std::to_string(max_line_length);
    }
    if (!is_valid_utf8(line)) {
        return std::string("invalid UTF-8");
    }
    return std::nullopt;
}

// Сообщение об отказе по числу аргументов: «/join expects 1 argument».
[[nodiscard]] std::string arity(std::string_view verb, std::string_view expected) {
    return std::string(verb) + " expects " + std::string(expected);
}

// Список комнат /msg: «#a,#b,#c». Ошибка — причина отказа строкой; это тот
// же Result, что и у разбора, только с ошибкой попроще.
[[nodiscard]] Result<Rooms, std::string> split_rooms(std::string_view list) {
    Rooms rooms;
    std::size_t from = 0;
    while (true) {
        const std::size_t comma = list.find(',', from);
        const std::string_view room =
            list.substr(from, comma == std::string_view::npos ? std::string_view::npos
                                                              : comma - from);
        if (const auto problem = room_name_problem(room); problem) {
            return Unexpected{*problem};
        }
        // Одна комната дважды — ошибка ввода, а не повод отправить дважды.
        if (std::find(rooms.begin(), rooms.end(), room) != rooms.end()) {
            return Unexpected{"room listed twice: " + std::string(room)};
        }
        rooms.push_back(room);
        if (comma == std::string_view::npos) {
            return rooms;
        }
        from = comma + 1;
    }
}

constexpr std::size_t default_history_limit = 10;

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

    // Особенность чата: у /msg текст сообщения — весь остаток строки после
    // списка комнат. Поэтому сначала смотрим на первое слово, а уже потом
    // решаем, где у строки хвост.
    const Tokens head = tokenize(line, 2);
    if (head.empty()) {
        fail("empty command", line);
    }
    const std::string_view verb = head.front();

    if (is(verb, "/msg")) {
        const Tokens tokens = tokenize(line, 3);
        if (tokens.size() < 2) {
            fail(arity(verb, "rooms and text"), line);
        }
        auto rooms = split_rooms(tokens[1]);
        if (!rooms) {
            fail(rooms.error(), line);
        }
        if (tokens.size() < 3) {
            fail("message text is empty", line);
        }
        if (const auto problem = text_problem(tokens[2]); problem) {
            fail(*problem, line);
        }
        return MsgCommand{std::move(rooms).value(), tokens[2]};
    }

    const Tokens tokens = tokenize(line);
    const std::size_t argc = tokens.size() - 1;

    if (is(verb, "/nick")) {
        if (argc != 1) {
            fail(arity(verb, "1 argument"), line);
        }
        if (const auto problem = nick_problem(tokens[1]); problem) {
            fail(*problem, line);
        }
        return NickCommand{tokens[1]};
    }

    // У всех остальных команд первый аргумент — комната.
    const bool known = is(verb, "/join") || is(verb, "/leave") || is(verb, "/who") ||
                       is(verb, "/history") || is(verb, "/type") || is(verb, "/react") ||
                       is(verb, "/topic");
    if (!known) {
        fail("unknown command: " + std::string(verb), line);
    }
    if (argc == 0) {
        fail(arity(verb, "a room"), line);
    }
    const std::string_view room = tokens[1];
    if (const auto problem = room_name_problem(room); problem) {
        fail(*problem, line);
    }

    if (is(verb, "/join") || is(verb, "/leave") || is(verb, "/who")) {
        if (argc != 1) {
            fail(arity(verb, "1 argument"), line);
        }
        if (is(verb, "/join")) {
            return JoinCommand{room};
        }
        if (is(verb, "/leave")) {
            return LeaveCommand{room};
        }
        return WhoCommand{room};
    }
    if (is(verb, "/history")) {
        if (argc > 2) {
            fail(arity(verb, "a room and an optional limit"), line);
        }
        if (argc == 1) {
            return HistoryCommand{room, default_history_limit};
        }
        const auto limit = to_number<std::size_t>(tokens[2]);
        if (!limit) {
            fail("limit must be a non-negative number: " + std::string(tokens[2]), line);
        }
        return HistoryCommand{room, *limit};
    }
    if (is(verb, "/type")) {
        if (argc != 2) {
            fail(arity(verb, "a room and an index"), line);
        }
        const auto index = to_number<std::size_t>(tokens[2]);
        if (!index) {
            fail("index must be a non-negative number: " + std::string(tokens[2]), line);
        }
        return TypeCommand{room, *index};
    }
    if (is(verb, "/react")) {
        if (argc != 2 && argc != 3) {
            fail(arity(verb, "a room, an index and an optional step"), line);
        }
        const auto index = to_number<std::size_t>(tokens[2]);
        if (!index) {
            fail("index must be a non-negative number: " + std::string(tokens[2]), line);
        }
        const auto by = argc == 3 ? to_number<std::int64_t>(tokens[3])
                                  : std::optional<std::int64_t>{1};
        if (!by) {
            fail("step must be a number: " + std::string(tokens[3]), line);
        }
        return ReactCommand{room, *index, *by};
    }
    // Осталась /topic. Тема из нескольких слов обязана быть в кавычках:
    // /topic #general "обсуждаем курс" — ровно три токена. Без кавычек слов
    // было бы больше, и команда не разберётся — это лучше, чем молча взять
    // первое.
    if (argc > 2) {
        fail(arity(verb, "a room and an optional quoted topic"), line);
    }
    if (argc == 1) {
        return TopicCommand{room, std::nullopt};
    }
    if (const auto problem = text_problem(tokens[2]); problem) {
        fail(*problem, line);
    }
    return TopicCommand{room, tokens[2]};
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

    if (is(verb, "/msg")) {
        const auto tokens = try_tokenize(line, 3);
        if (!tokens) {
            return Unexpected{tokens.error()};
        }
        if (tokens->size() < 2) {
            return failure(arity(verb, "rooms and text"), line);
        }
        auto rooms = split_rooms((*tokens)[1]);
        if (!rooms) {
            return failure(rooms.error(), line);
        }
        if (tokens->size() < 3) {
            return failure("message text is empty", line);
        }
        if (const auto problem = text_problem((*tokens)[2]); problem) {
            return failure(*problem, line);
        }
        return Command{MsgCommand{std::move(rooms).value(), (*tokens)[2]}};
    }

    const auto all = try_tokenize(line);
    if (!all) {
        return Unexpected{all.error()};
    }
    const Tokens& tokens = *all;
    const std::size_t argc = tokens.size() - 1;

    if (is(verb, "/nick")) {
        if (argc != 1) {
            return failure(arity(verb, "1 argument"), line);
        }
        if (const auto problem = nick_problem(tokens[1]); problem) {
            return failure(*problem, line);
        }
        return Command{NickCommand{tokens[1]}};
    }

    const bool known = is(verb, "/join") || is(verb, "/leave") || is(verb, "/who") ||
                       is(verb, "/history") || is(verb, "/type") || is(verb, "/react") ||
                       is(verb, "/topic");
    if (!known) {
        return failure("unknown command: " + std::string(verb), line);
    }
    if (argc == 0) {
        return failure(arity(verb, "a room"), line);
    }
    const std::string_view room = tokens[1];
    if (const auto problem = room_name_problem(room); problem) {
        return failure(*problem, line);
    }

    if (is(verb, "/join") || is(verb, "/leave") || is(verb, "/who")) {
        if (argc != 1) {
            return failure(arity(verb, "1 argument"), line);
        }
        if (is(verb, "/join")) {
            return Command{JoinCommand{room}};
        }
        if (is(verb, "/leave")) {
            return Command{LeaveCommand{room}};
        }
        return Command{WhoCommand{room}};
    }
    if (is(verb, "/history")) {
        if (argc > 2) {
            return failure(arity(verb, "a room and an optional limit"), line);
        }
        if (argc == 1) {
            return Command{HistoryCommand{room, default_history_limit}};
        }
        const auto limit = to_number<std::size_t>(tokens[2]);
        if (!limit) {
            return failure("limit must be a non-negative number: " + std::string(tokens[2]),
                           line);
        }
        return Command{HistoryCommand{room, *limit}};
    }
    if (is(verb, "/type")) {
        if (argc != 2) {
            return failure(arity(verb, "a room and an index"), line);
        }
        const auto index = to_number<std::size_t>(tokens[2]);
        if (!index) {
            return failure("index must be a non-negative number: " + std::string(tokens[2]),
                           line);
        }
        return Command{TypeCommand{room, *index}};
    }
    if (is(verb, "/react")) {
        if (argc != 2 && argc != 3) {
            return failure(arity(verb, "a room, an index and an optional step"), line);
        }
        const auto index = to_number<std::size_t>(tokens[2]);
        if (!index) {
            return failure("index must be a non-negative number: " + std::string(tokens[2]),
                           line);
        }
        const auto by = argc == 3 ? to_number<std::int64_t>(tokens[3])
                                  : std::optional<std::int64_t>{1};
        if (!by) {
            return failure("step must be a number: " + std::string(tokens[3]), line);
        }
        return Command{ReactCommand{room, *index, *by}};
    }
    if (argc > 2) {
        return failure(arity(verb, "a room and an optional quoted topic"), line);
    }
    if (argc == 1) {
        return Command{TopicCommand{room, std::nullopt}};
    }
    if (const auto problem = text_problem(tokens[2]); problem) {
        return failure(*problem, line);
    }
    return Command{TopicCommand{room, tokens[2]}};
}

}  // namespace chat
