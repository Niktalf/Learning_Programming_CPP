#include "chat/event_wire.hpp"

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "chat/overloaded.hpp"

namespace chat {

namespace {

constexpr char separator = '\t';

[[nodiscard]] std::string escape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '\t': escaped += "\\t"; break;
        case '\n': escaped += "\\n"; break;
        case '\\': escaped += "\\\\"; break;
        default: escaped += c;
        }
    }
    return escaped;
}

[[nodiscard]] std::optional<std::string> unescape(std::string_view text) {
    std::string plain;
    plain.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\') {
            plain += text[i];
            continue;
        }
        if (++i == text.size()) {
            return std::nullopt;   // косая черта в конце поля
        }
        switch (text[i]) {
        case 't': plain += '\t'; break;
        case 'n': plain += '\n'; break;
        case '\\': plain += '\\'; break;
        default: return std::nullopt;
        }
    }
    return plain;
}

[[nodiscard]] std::vector<std::string_view> split(std::string_view payload) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = payload.find(separator, start);
        fields.push_back(payload.substr(start, end - start));
        if (end == std::string_view::npos) {
            return fields;
        }
        start = end + 1;
    }
}

[[nodiscard]] std::optional<std::uint64_t> to_number(std::string_view digits) {
    std::uint64_t value = 0;
    const char* const last = digits.data() + digits.size();
    const auto [end, error] = std::from_chars(digits.data(), last, value);
    if (digits.empty() || error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::string EventEnvelope::id() const {
    return std::format("{}:{}:{}", publisher, room_of(event), number);
}

std::string encode_event(const EventEnvelope& envelope) {
    const std::string head = std::format("{}\t{}", escape(envelope.publisher), envelope.number);
    return std::visit(
        overloaded{
            [&](const MessagePosted& e) {
                return std::format("{}\tposted\t{}\t{}\t{}\t{}\t{}", head, escape(e.room), escape(e.author),
                                   escape(e.kind), e.seq, escape(e.text));
            },
            [&](const UserJoined& e) {
                return std::format("{}\tjoined\t{}\t{}", head, escape(e.room), escape(e.nick));
            },
            [&](const UserLeft& e) { return std::format("{}\tleft\t{}\t{}", head, escape(e.room), escape(e.nick)); },
        },
        envelope.event);
}

std::optional<EventEnvelope> decode_event(std::string_view payload) {
    const std::vector<std::string_view> fields = split(payload);
    if (fields.size() < 5) {
        return std::nullopt;
    }
    std::optional<std::string> publisher = unescape(fields[0]);
    const std::optional<std::uint64_t> number = to_number(fields[1]);
    std::optional<std::string> room = unescape(fields[3]);
    std::optional<std::string> who = unescape(fields[4]);
    if (!publisher || publisher->empty() || !number || !room || !who) {
        return std::nullopt;
    }
    const std::string_view kind = fields[2];
    if (kind == "posted" && fields.size() == 8) {
        std::optional<std::string> message_kind = unescape(fields[5]);
        const std::optional<std::uint64_t> seq = to_number(fields[6]);
        std::optional<std::string> text = unescape(fields[7]);
        if (!message_kind || !seq || !text) {
            return std::nullopt;
        }
        return EventEnvelope{std::move(*publisher), *number,
                             MessagePosted{std::move(*room), std::move(*who), std::move(*text),
                                           std::move(*message_kind), *seq}};
    }
    if (kind == "joined" && fields.size() == 5) {
        return EventEnvelope{std::move(*publisher), *number, UserJoined{std::move(*room), std::move(*who)}};
    }
    if (kind == "left" && fields.size() == 5) {
        return EventEnvelope{std::move(*publisher), *number, UserLeft{std::move(*room), std::move(*who)}};
    }
    return std::nullopt;
}

std::string describe_event(const DomainEvent& event) {
    return std::visit(
        overloaded{
            [](const MessagePosted& e) {
                return std::format("{} <{}> {} (#{}, {})", e.room, e.author, e.text, e.seq, e.kind);
            },
            [](const UserJoined& e) { return std::format("{} + {} joined", e.room, e.nick); },
            [](const UserLeft& e) { return std::format("{} - {} left", e.room, e.nick); },
        },
        event);
}

}  // namespace chat
