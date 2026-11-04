#include "taskq/executor.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

#include "taskq/overloaded.hpp"

namespace taskq {
namespace {

constexpr std::string_view number_prefix = "num:";
constexpr std::string_view binary_prefix = "bin:";

[[nodiscard]] std::optional<std::int64_t> to_int(std::string_view text) noexcept {
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

[[nodiscard]] std::optional<std::string> from_hex(std::string_view text) {
    if (text.size() % 2 != 0) {
        return std::nullopt;
    }
    std::string bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = hex_digit(text[i]);
        const int low = hex_digit(text[i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<char>(high * 16 + low));
    }
    return bytes;
}

// Тело задачи по префиксу: num:42 — числовой аргумент, bin:cafe — двоичные
// данные в шестнадцатеричной записи, всё остальное — текстовая команда.
[[nodiscard]] std::optional<Payload> make_payload(std::string_view body) {
    if (body.substr(0, number_prefix.size()) == number_prefix) {
        if (const auto value = to_int(body.substr(number_prefix.size())); value) {
            return Payload{NumericPayload{*value}};
        }
        return std::nullopt;
    }
    if (body.substr(0, binary_prefix.size()) == binary_prefix) {
        if (auto bytes = from_hex(body.substr(binary_prefix.size())); bytes) {
            return Payload{BinaryPayload{std::move(*bytes)}};
        }
        return std::nullopt;
    }
    return Payload{TextPayload{std::string(body)}};
}

[[nodiscard]] Response unknown_task(TaskId id) {
    std::ostringstream out;
    out << "ERR no such task: " << id;
    return Response{false, out.str()};
}

[[nodiscard]] Response not_in_flight(TaskId id) {
    std::ostringstream out;
    out << "ERR task " << id << " is not in progress";
    return Response{false, out.str()};
}

}  // namespace

Response Service::execute(const Command& command) {
    return std::visit(
        overloaded{
            [&](const SubmitCommand& c) {
                auto payload = make_payload(c.body);
                if (!payload) {
                    return Response{false, "ERR malformed num: or bin: payload"};
                }
                const TaskId id = next_id_++;
                store_.submit(std::string(c.queue), Task(id, std::move(*payload), c.priority));
                std::ostringstream out;
                out << "OK id=" << id;
                return Response{true, out.str()};
            },
            [&](const PollCommand& c) {
                // if с инициализацией: результат ограничен областью проверки.
                if (auto task = store_.take(c.queue); task) {
                    const TaskId id = task->id();
                    std::ostringstream out;
                    out << "id=" << id << ' ' << task->to_string();
                    in_flight_.emplace(id, std::move(*task));
                    return Response{true, out.str()};
                }
                // Здесь и виден изъян: сказать, очередь пуста или её нет
                // вовсе, мы по-прежнему не можем. Это чинится на этапе 3.
                return Response{true, "(nil)"};
            },
            [&](const AckCommand& c) {
                if (in_flight_.erase(c.id) == 0) {
                    return not_in_flight(c.id);
                }
                finished_.insert_or_assign(c.id, true);
                return Response{true, "OK"};
            },
            [&](const FailCommand& c) {
                if (in_flight_.erase(c.id) == 0) {
                    return not_in_flight(c.id);
                }
                finished_.insert_or_assign(c.id, false);
                return Response{true, "OK"};
            },
            [&](const StatusCommand& c) {
                if (const auto queue = store_.queue_of(c.id); queue) {
                    return Response{true, "pending in " + *queue};
                }
                if (in_flight_.count(c.id) != 0) {
                    return Response{true, "in progress"};
                }
                if (const auto it = finished_.find(c.id); it != finished_.end()) {
                    return Response{true, it->second ? "done" : "failed"};
                }
                return unknown_task(c.id);
            },
            [&](const TypeCommand& c) {
                if (const auto task = store_.find(c.id); task) {
                    return Response{true, std::string(task->get().type_name())};
                }
                if (const auto it = in_flight_.find(c.id); it != in_flight_.end()) {
                    return Response{true, std::string(it->second.type_name())};
                }
                return unknown_task(c.id);
            },
            [&](const BumpCommand& c) {
                // BUMP меняет ещё не выданную задачу: менять аргумент работы,
                // которую воркер уже выполняет, бессмысленно.
                const auto task = store_.find(c.id);
                if (!task) {
                    return unknown_task(c.id);
                }
                // get_if вместо std::get: неверная альтернатива здесь штатная
                // ошибка пользователя, а не исключительная ситуация.
                NumericPayload* const number = task->get().get_if<NumericPayload>();
                if (number == nullptr) {
                    return Response{false, "ERR BUMP needs a number, the payload is " +
                                               std::string(task->get().type_name())};
                }
                number->value += c.by;
                std::ostringstream out;
                out << number->value;
                return Response{true, out.str()};
            },
        },
        command);
}

}  // namespace taskq
