#include "kvs/event_wire.hpp"

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "kvs/overloaded.hpp"

namespace kvs {

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

std::string encode_event(const EventEnvelope& envelope) {
    const std::string id = escape(envelope.id);
    return std::visit(
        overloaded{
            [&](const KeyChanged& e) {
                return std::format("{}\tchanged\t{}\t{}\t{}", id, escape(e.key), escape(e.type), e.generation);
            },
            [&](const KeyDeleted& e) { return std::format("{}\tdeleted\t{}", id, escape(e.key)); },
            [&](const KeyExpired& e) { return std::format("{}\texpired\t{}", id, escape(e.key)); },
        },
        envelope.event);
}

std::optional<EventEnvelope> decode_event(std::string_view payload) {
    const std::vector<std::string_view> fields = split(payload);
    if (fields.size() < 3) {
        return std::nullopt;
    }
    std::optional<std::string> id = unescape(fields[0]);
    std::optional<std::string> key = unescape(fields[2]);
    if (!id || id->empty() || !key) {
        return std::nullopt;
    }
    const std::string_view kind = fields[1];
    if (kind == "changed" && fields.size() == 5) {
        std::optional<std::string> type = unescape(fields[3]);
        const std::optional<std::uint64_t> generation = to_number(fields[4]);
        if (!type || !generation) {
            return std::nullopt;
        }
        return EventEnvelope{std::move(*id), KeyChanged{std::move(*key), std::move(*type), *generation}};
    }
    if (kind == "deleted" && fields.size() == 3) {
        return EventEnvelope{std::move(*id), KeyDeleted{std::move(*key)}};
    }
    if (kind == "expired" && fields.size() == 3) {
        return EventEnvelope{std::move(*id), KeyExpired{std::move(*key)}};
    }
    return std::nullopt;
}

std::string describe_event(const DomainEvent& event) {
    return std::visit(
        overloaded{
            [](const KeyChanged& e) {
                return std::format("changed {} ({}, generation {})", e.key, e.type, e.generation);
            },
            [](const KeyDeleted& e) { return std::format("deleted {}", e.key); },
            [](const KeyExpired& e) { return std::format("expired {}", e.key); },
        },
        event);
}

}  // namespace kvs
