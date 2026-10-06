#include "parser.hpp"

#include <charconv>
#include <cstddef>
#include <string>

#include "lexer.hpp"

namespace {
    [[nodiscard]] bool is(const std::string_view token, const std::string_view name) noexcept {
        if (token.size() != name.size()) {
            return false;
        }
        for (std::size_t i = 0; i < token.size(); ++i) {
            if (const char c = (token[i] >= 'A' && token[i] <= 'Z') ? static_cast<char>(token[i] + 32) : token[i];
                c != name[i]
            ) {
                return false;
            }
        }
        return true;
    }

    [[noreturn]] void fail(std::string reason, std::string_view line) {
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

    [[nodiscard]] bool is_room(const std::string_view token) noexcept {
        return token.size() > 1 && token.front() == '#';
    }

    [[nodiscard]] Command parse_msg(std::string_view line) {
        std::size_t i = line.find(' ');
        MsgCommand msg;
        while (i != std::string_view::npos && i < line.size()) {
            while (i < line.size() && line[i] == ' ') {
                ++i;
            }
            if (i >= line.size()) {
                break;
            }
            const auto end = line.find(' ', i);
            const std::string_view word =
                line.substr(i, end == std::string_view::npos ? line.size() - i : end - i);
            if (!is_room(word)) {
                msg.text = line.substr(i);
                break;
            }
            msg.rooms.push_back(word);
            i = end;
        }
        if (msg.rooms.empty()) {
            fail("/msg expects at least one #room", line);
        }
        if (msg.text.empty()) {
            fail("/msg expects message text", line);
        }
        return msg;
    }
}  // namespace

Command parse(const InputLine& line) {
    const std::string_view view = line.view();
    const Tokens head = tokenize(view, 2);
    if (head.empty()) {
        fail("empty command", view);
    }
    if (is(head.front(), "/msg")) {
        return parse_msg(view);
    }

    const Tokens tokens = tokenize(view);
    const std::string_view verb = tokens.front();
    const auto argc = tokens.size() - 1;

    if (is(verb, "/join")) {
        if (argc != 1 || !is_room(tokens[1])) fail("/join expects #room", view);
        return JoinCommand {
            .room = tokens[1]
        };
    }
    if (is(verb, "/leave")) {
        if (argc != 1 || !is_room(tokens[1])) fail("/leave expects #room", view);
        return LeaveCommand {
            .room = tokens[1]
        };
    }
    if (is(verb, "/rooms")) {
        if (argc != 0) fail("/rooms takes no arguments", view);
        return RoomsCommand {};
    }
    if (is(verb, "/nick")) {
        if (argc != 1) fail("/nick expects a name", view);
        return NickCommand {
            .nick = tokens[1]
        };
    }
    if (is(verb, "/history")) {
        if ((argc != 1 && argc != 2) || !is_room(tokens[1])) {
            fail("/history expects #room [limit]", view);
        }
        std::size_t limit = 20;
        if (argc == 2) {
            limit = to_number<std::size_t>(tokens[2], view);
        }
        return HistoryCommand {
            .room = tokens[1],
            .limit = limit
        };
    }
    if (is(verb, "/type")) {
        if (argc != 2 || !is_room(tokens[1])) fail("/type expects #room index", view);
        return TypeCommand {
            .room = tokens[1],
            .index = to_number<std::size_t>(tokens[2], view)
        };
    }
    if (is(verb, "/react")) {
        if ((argc != 2 && argc != 3) || !is_room(tokens[1])) {
            fail("/react expects #room index [by]", view);
        }
        const auto index = to_number<std::size_t>(tokens[2], view);
        const std::int64_t by = argc == 3 ? to_number<std::int64_t>(tokens[3], view) : 1;
        return ReactCommand {
            .room = tokens[1],
            .index = index,
            .by = by
        };
    }
    if (is(verb, "/logout")) {
        if (argc != 1) fail("/logout expects a name", view);
        return LogoutCommand{
            .nick = tokens[1]
        };
    }
    if (is(verb, "/invite")) {
        if (argc != 2 || !is_room(tokens[1])) fail("/invite expects #room name", view);
        return InviteCommand{
            .room = tokens[1],
            .nick = tokens[2]
        };
    }
    if (is(verb, "/who")) {
        if (argc != 1 || !is_room(tokens[1])) fail("/who expects #room", view);
        return WhoCommand{
            .room = tokens[1]
        };
    }
    fail("unknown command: " + std::string(verb), view);
}
