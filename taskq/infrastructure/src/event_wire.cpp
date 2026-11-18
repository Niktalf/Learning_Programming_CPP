#include "taskq/event_wire.hpp"

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "taskq/overloaded.hpp"

namespace taskq {

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

[[nodiscard]] std::optional<int> to_int(std::string_view digits) {
    int value = 0;
    const char* const last = digits.data() + digits.size();
    const auto [end, error] = std::from_chars(digits.data(), last, value);
    if (digits.empty() || error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::string EventEnvelope::id() const {
    return std::format("{}:{}:{}", publisher, task_of(event), number);
}

std::string encode_event(const EventEnvelope& envelope) {
    const std::string head = std::format("{}\t{}", escape(envelope.publisher), envelope.number);
    return std::visit(
        overloaded{
            [&](const TaskSubmitted& e) {
                return std::format("{}\tsubmitted\t{}\t{}\t{}", head, e.id, escape(e.queue), e.priority);
            },
            [&](const TaskLeased& e) { return std::format("{}\tleased\t{}\t{}", head, e.id, escape(e.worker)); },
            [&](const TaskCompleted& e) { return std::format("{}\tcompleted\t{}", head, e.id); },
            [&](const TaskFailed& e) { return std::format("{}\tfailed\t{}\t{}", head, e.id, escape(e.reason)); },
            [&](const TaskReturned& e) {
                return std::format("{}\treturned\t{}\t{}", head, e.id, escape(e.reason));
            },
            [&](const TaskCancelled& e) { return std::format("{}\tcancelled\t{}", head, e.id); },
        },
        envelope.event);
}

std::optional<EventEnvelope> decode_event(std::string_view payload) {
    const std::vector<std::string_view> fields = split(payload);
    if (fields.size() < 4) {
        return std::nullopt;
    }
    std::optional<std::string> publisher = unescape(fields[0]);
    const std::optional<std::uint64_t> number = to_number(fields[1]);
    const std::optional<TaskId> id = to_number(fields[3]);
    if (!publisher || publisher->empty() || !number || !id) {
        return std::nullopt;
    }
    const auto envelope = [&](DomainEvent event) {
        return EventEnvelope{std::move(*publisher), *number, std::move(event)};
    };
    const std::string_view kind = fields[2];
    if (fields.size() == 4) {
        if (kind == "completed") {
            return envelope(TaskCompleted{*id});
        }
        if (kind == "cancelled") {
            return envelope(TaskCancelled{*id});
        }
        return std::nullopt;
    }
    std::optional<std::string> text = unescape(fields[4]);
    if (!text) {
        return std::nullopt;
    }
    if (kind == "submitted" && fields.size() == 6) {
        const std::optional<int> priority = to_int(fields[5]);
        if (!priority) {
            return std::nullopt;
        }
        return envelope(TaskSubmitted{*id, std::move(*text), *priority});
    }
    if (fields.size() != 5) {
        return std::nullopt;
    }
    if (kind == "leased") {
        return envelope(TaskLeased{*id, std::move(*text)});
    }
    if (kind == "failed") {
        return envelope(TaskFailed{*id, std::move(*text)});
    }
    if (kind == "returned") {
        return envelope(TaskReturned{*id, std::move(*text)});
    }
    return std::nullopt;
}

std::string describe_event(const DomainEvent& event) {
    return std::visit(
        overloaded{
            [](const TaskSubmitted& e) {
                return std::format("task {} submitted to {} (priority {})", e.id, e.queue, e.priority);
            },
            [](const TaskLeased& e) {
                return e.worker.empty() ? std::format("task {} leased", e.id)
                                        : std::format("task {} leased by {}", e.id, e.worker);
            },
            [](const TaskCompleted& e) { return std::format("task {} completed", e.id); },
            [](const TaskFailed& e) { return std::format("task {} failed ({})", e.id, e.reason); },
            [](const TaskReturned& e) { return std::format("task {} returned ({})", e.id, e.reason); },
            [](const TaskCancelled& e) { return std::format("task {} cancelled", e.id); },
        },
        event);
}

}  // namespace taskq
