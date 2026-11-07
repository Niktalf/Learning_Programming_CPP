#pragma once

#include <charconv>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/task.hpp"
#include "taskq/validate.hpp"

namespace taskq {

// Этап 6: превращение токена команды в аргумент обработчика нужного типа.
//
// Типы-обёртки несут смысл, которого нет у string_view: QueueName — уже
// проверенное имя очереди, и обработчику не нужно проверять его заново.
// Проверка живёт в одном месте — в convert, — а не в каждой команде.
struct QueueName {
    std::string_view name;
};

struct WorkerName {
    std::string_view name;
};

// Именованные аргументы: распознаются по префиксу, а не по позиции. Поэтому
// необязательный prio=N может стоять перед телом SUBMIT — тело тоже начинается
// с какого-то слова, и отличить одно от другого можно только по префиксу.
// Именованные аргументы, идущие подряд, принимаются в любом порядке: LIST
// build limit=5 prio>=3 и LIST build prio>=3 limit=5 — одна команда.
struct Priority {
    static constexpr std::string_view prefix = "prio=";
    int value;
};

struct MinPriority {
    static constexpr std::string_view prefix = "prio>=";
    int value;
};

struct Kind {
    static constexpr std::string_view prefix = "type=";
    std::string_view name;
};

struct Limit {
    static constexpr std::string_view prefix = "limit=";
    std::size_t value;
};

// Маркер «весь остаток строки»: тело SUBMIT. Остаток не разбивается на
// токены и не разбирает кавычки — echo "hi уходит как есть. Только
// последним параметром.
struct Tail {
    std::string_view text;
};

inline constexpr int max_priority = 9;

// Аргумент из всех оставшихся токенов: тип с from_tokens. Только последним
// параметром. Так устроен пакет SUBMITBATCH — групп «очередь [prio=N] тело»
// сколько угодно, и фиксированной арностью их не выразить. Разбор пакета —
// тоже при bind, а не при исполнении: ошибка в третьей группе — ошибка
// разбора с подсказкой, как и у любого другого аргумента.
template <class T>
concept TokenList = requires(std::vector<std::string_view> tokens) {
    { T::from_tokens(std::move(tokens)) } -> std::same_as<T>;
};

struct Batch {
    struct Item {
        std::string_view queue;
        int priority;
        std::string_view body;   // один токен: несколько слов — в кавычках
    };
    std::vector<Item> items;

    // Бросает ParseError. Определена в src/convert.cpp.
    [[nodiscard]] static Batch from_tokens(std::vector<std::string_view> tokens);
};

template <class T>
inline constexpr bool is_optional_v = false;

template <class T>
inline constexpr bool is_optional_v<std::optional<T>> = true;

template <class T>
concept Named = requires {
    { T::prefix } -> std::convertible_to<std::string_view>;
};

// Типы, которые умеет convert. Список закрытый: любой другой тип параметра
// обработчика — ошибка при регистрации команды, а не при её вызове.
//
// TaskId — псевдоним std::uint64_t, а не отдельный тип, и это стоит
// держать в голове: любой параметр uint64_t разбирается как номер задачи.
// Поэтому у LIST предел — Limit, а не size_t (на Linux это тот же uint64_t).
template <class T>
concept ScalarArgument =
    std::same_as<T, std::string_view> || std::same_as<T, QueueName> || std::same_as<T, WorkerName> ||
    std::same_as<T, Priority> || std::same_as<T, MinPriority> || std::same_as<T, Kind> ||
    std::same_as<T, Limit> || std::same_as<T, std::chrono::seconds> ||
    (std::is_arithmetic_v<T> && !std::same_as<T, bool>);

// Необязательным может быть только скалярный аргумент. Tail обязателен по
// смыслу — тело задачи, — а пуст ли список токенов, решает сам TokenList.
template <class T>
concept Argument = ScalarArgument<T> || std::same_as<T, Tail> || TokenList<T> ||
                   (is_optional_v<T> && ScalarArgument<typename T::value_type>);

namespace detail {

[[noreturn]] inline void reject(std::string reason, std::string_view token) {
    throw ParseError(std::move(reason), std::string(token));
}

// Число целиком: from_chars и проверка, что разобран весь токен. Без
// проверки end != last «12abc» разобралось бы как 12 — from_chars разбирает
// префикс и сообщает об успехе.
template <class T>
[[nodiscard]] std::optional<T> number(std::string_view token) noexcept {
    T value{};
    const char* const last = token.data() + token.size();
    const auto [end, error] = std::from_chars(token.data(), last, value);
    if (error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] inline int priority(std::string_view digits) {
    const auto value = number<int>(digits);
    if (!value || *value < 0 || *value > max_priority) {
        reject("priority must be 0.." + std::to_string(max_priority) + ": " + std::string(digits),
               digits);
    }
    return *value;
}

}  // namespace detail

// Разбор одного токена. Ветки if constexpr проверяются по порядку, и порядок
// существен: специальное — раньше общего. Ветка TaskId стоит выше ветки
// is_arithmetic_v, и иначе нельзя: TaskId — тот же std::uint64_t, ветка для
// чисел подходит и ему, и до проверки формата номера очередь бы не дошла.
//
// Именованному аргументу приходит значение без префикса: «5» из «prio=5».
template <ScalarArgument T>
[[nodiscard]] T convert(std::string_view token) {
    if constexpr (std::is_same_v<T, QueueName>) {
        if (const auto problem = queue_name_problem(token); problem) {
            detail::reject(*problem, token);
        }
        return QueueName{token};
    } else if constexpr (std::is_same_v<T, WorkerName>) {
        if (const auto problem = worker_name_problem(token); problem) {
            detail::reject(*problem, token);
        }
        return WorkerName{token};
    } else if constexpr (std::is_same_v<T, Priority>) {
        return Priority{detail::priority(token)};
    } else if constexpr (std::is_same_v<T, MinPriority>) {
        return MinPriority{detail::priority(token)};
    } else if constexpr (std::is_same_v<T, Kind>) {
        if (token != "text" && token != "number" && token != "binary") {
            detail::reject("type must be text, number or binary: " + std::string(token), token);
        }
        return Kind{token};
    } else if constexpr (std::is_same_v<T, Limit>) {
        const auto value = detail::number<std::size_t>(token);
        if (!value) {
            detail::reject("limit must be a non-negative number: " + std::string(token), token);
        }
        return Limit{*value};
    } else if constexpr (std::is_same_v<T, std::chrono::seconds>) {
        // Задержка DELAY: число с необязательной единицей — 90, 90s, 15m, 2h.
        const char unit = token.empty() ? '\0' : token.back();
        const bool has_unit = unit == 's' || unit == 'm' || unit == 'h';
        const auto value = detail::number<std::int64_t>(has_unit ? token.substr(0, token.size() - 1) : token);
        constexpr std::int64_t max_hours = 24 * 365;   // не дальше года: дальше — опечатка
        const std::int64_t scale = unit == 'h' ? 3600 : unit == 'm' ? 60 : 1;
        if (!value || *value < 0 || *value > max_hours * 3600 / scale) {
            detail::reject("delay must be like 90, 90s, 15m or 2h, at most a year: " + std::string(token),
                           token);
        }
        return std::chrono::seconds(*value * scale);
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        return token;
    } else if constexpr (std::is_same_v<T, TaskId>) {
        // Формат номера: только цифры, без знака и ведущих нулей, не ноль —
        // номера выдаются с единицы. «+5» и «007» from_chars принял бы или
        // разобрал бы как 7; номер задачи так не пишут, и это скорее
        // опечатка.
        const bool digits_only =
            !token.empty() && token.front() != '0' &&
            token.find_first_not_of("0123456789") == std::string_view::npos;
        const auto value = digits_only ? detail::number<TaskId>(token) : std::nullopt;
        if (!value) {
            detail::reject("task id must be a positive number: " + std::string(token), token);
        }
        return *value;
    } else if constexpr (std::is_arithmetic_v<T>) {
        const auto value = detail::number<T>(token);
        if (!value) {
            detail::reject("not a number: " + std::string(token), token);
        }
        return *value;
    } else {
        // static_assert(false) в отброшенной ветке законен только с C++23
        // (P2593). Раньше он срабатывал при разборе шаблона, даже если ветка
        // не выбиралась. Сюда концепция ScalarArgument и так не пропустит.
        static_assert(false, "convert: unsupported argument type");
    }
}

// Как аргумент выглядит в подсказке «usage: …». Тоже if constexpr по типу:
// подсказка выводится из сигнатуры обработчика и разойтись с ней не может.
template <class T>
[[nodiscard]] constexpr std::string_view placeholder() noexcept {
    if constexpr (std::is_same_v<T, QueueName>) {
        return "queue";
    } else if constexpr (std::is_same_v<T, WorkerName>) {
        return "worker";
    } else if constexpr (std::is_same_v<T, Priority>) {
        return "prio=0..9";
    } else if constexpr (std::is_same_v<T, MinPriority>) {
        return "prio>=0..9";
    } else if constexpr (std::is_same_v<T, Kind>) {
        return "type=text|number|binary";
    } else if constexpr (std::is_same_v<T, Limit>) {
        return "limit=N";
    } else if constexpr (std::is_same_v<T, std::chrono::seconds>) {
        return "delay";
    } else if constexpr (std::is_same_v<T, Tail>) {
        return "body...";
    } else if constexpr (std::is_same_v<T, Batch>) {
        return "<queue> [prio=0..9] <body> ...";
    } else if constexpr (std::is_same_v<T, TaskId>) {
        return "id";
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        return "word";
    } else {
        return "number";
    }
}

}  // namespace taskq
