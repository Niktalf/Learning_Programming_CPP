#include "chat/parser.hpp"

#include <charconv>
#include <cstdint>

#include "chat/lexer.hpp"

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

[[nodiscard]] bool is_room(std::string_view token) noexcept {
    return token.size() > 1 && token.front() == '#';
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

}  // namespace

std::optional<Command> parse(const InputLine& line) {
    // Особенность чата: у /msg текст сообщения — весь остаток строки после
    // имени комнаты. Поэтому сначала смотрим на первое слово, а уже потом
    // решаем, где у строки хвост.
    const Tokens head = tokenize(line.view(), 2);
    if (head.empty()) {
        return std::nullopt;
    }
    const bool is_msg = is(head.front(), "/msg");
    const Tokens tokens = is_msg ? tokenize(line.view(), 3) : tokenize(line.view());
    if (tokens.empty()) {
        return std::nullopt;   // незакрытая кавычка
    }

    const std::string_view verb = tokens.front();
    const std::size_t argc = tokens.size() - 1;
    if (argc == 0 || !is_room(tokens[1])) {
        return std::nullopt;   // у всех команд этапа первый аргумент — комната
    }
    const std::string_view room = tokens[1];

    if (is_msg && argc == 2 && !tokens[2].empty()) {
        return MsgCommand{room, tokens[2]};
    }
    if (is(verb, "/join") && argc == 1) {
        return JoinCommand{room};
    }
    if (is(verb, "/leave") && argc == 1) {
        return LeaveCommand{room};
    }
    if (is(verb, "/who") && argc == 1) {
        return WhoCommand{room};
    }
    if (is(verb, "/history") && (argc == 1 || argc == 2)) {
        // Без лимита показываем последние десять сообщений.
        const std::optional<std::size_t> limit =
            argc == 2 ? to_number<std::size_t>(tokens[2]) : std::optional<std::size_t>{10};
        if (!limit) {
            return std::nullopt;
        }
        return HistoryCommand{room, *limit};
    }
    if (is(verb, "/type") && argc == 2) {
        if (const auto index = to_number<std::size_t>(tokens[2]); index) {
            return TypeCommand{room, *index};
        }
        return std::nullopt;
    }
    if (is(verb, "/react") && (argc == 2 || argc == 3)) {
        const auto index = to_number<std::size_t>(tokens[2]);
        const std::optional<std::int64_t> by =
            argc == 3 ? to_number<std::int64_t>(tokens[3]) : std::optional<std::int64_t>{1};
        if (!index || !by) {
            return std::nullopt;
        }
        return ReactCommand{room, *index, *by};
    }
    if (is(verb, "/topic") && (argc == 1 || argc == 2)) {
        // Тема из нескольких слов обязана быть в кавычках: /topic #general
        // "обсуждаем курс" — ровно три токена. Без кавычек слов было бы
        // больше, и команда не разберётся — это лучше, чем молча взять первое.
        return TopicCommand{room, argc == 2 ? std::optional<std::string_view>{tokens[2]}
                                            : std::nullopt};
    }
    return std::nullopt;
}

}  // namespace chat
