#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "chat/convert.hpp"
#include "chat/errors.hpp"
#include "chat/function_traits.hpp"
#include "chat/lexer.hpp"
#include "chat/validate.hpp"

namespace chat {

// Ответ клиенту. Неудача внутри ядра — исключение; ok == false появляется
// только на границе handle, где исключение превращается в ответ.
struct Response {
    bool ok = true;
    std::string text;
};

namespace detail {

// Форма списка параметров обработчика после контекста:
//   - сначала обязательные, затем необязательные (std::optional);
//   - или обязательные, а последним — Tail.
// Tail и необязательные вместе нельзя: если необязательный аргумент
// пропущен, его место занял бы текст, и строку нельзя было бы разрезать
// однозначно. Проверяется при компиляции.
template <class... Params>
struct Shape {
    static constexpr std::size_t count = sizeof...(Params);
    static constexpr std::array<bool, count> optional = {is_optional_v<Params>...};
    static constexpr std::array<bool, count> tail = {std::is_same_v<Params, Tail>...};

    static constexpr std::size_t required = [] {
        std::size_t result = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (!optional[i]) {
                ++result;
            }
        }
        return result;
    }();

    static constexpr bool has_tail = count > 0 && tail[count - 1];

    static constexpr bool valid = [] {
        bool seen_optional = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (tail[i] && (i + 1 != count || seen_optional)) {
                return false;
            }
            if (optional[i]) {
                seen_optional = true;
            } else if (seen_optional) {
                return false;
            }
        }
        return true;
    }();
};

template <class Arguments>
struct HandlerParts {
    static constexpr bool has_context = false;
};

template <class First, class... Params>
struct HandlerParts<std::tuple<First, Params...>> {
    static constexpr bool has_context = true;
    using first = First;
    using params = std::tuple<Params...>;
    using shape = Shape<Params...>;
    static constexpr bool arguments_supported = (Argument<Params> && ...);
};

template <class F>
using parts_of = HandlerParts<typename function_traits<std::decay_t<F>>::arguments>;

// Требования по одному, каждое со своим именем: диагностика называет то,
// которое не выполнено, а не одно общее «не подходит».
template <class F>
concept HasSignature = requires { typename function_traits<std::decay_t<F>>::result; };

template <class F>
concept ReturnsResponse =
    std::convertible_to<typename function_traits<std::decay_t<F>>::result, Response>;

template <class F, class Context>
concept TakesContextFirst =
    parts_of<F>::has_context && std::same_as<typename parts_of<F>::first, Context&>;

template <class F>
concept ArgumentsSupported = parts_of<F>::arguments_supported;

template <class F>
concept ArgumentOrderValid = parts_of<F>::shape::valid;

// Аргумент номер I из токенов: optional — токен, если он есть, обычный тип
// — свой токен. Tail здесь — тоже свой токен: строку уже разрезали так, что
// последний токен — весь остаток.
template <class Param, std::size_t I>
[[nodiscard]] Param argument_at(std::span<const std::string_view> tokens) {
    if constexpr (is_optional_v<Param>) {
        if (I < tokens.size()) {
            return convert<typename Param::value_type>(tokens[I]);
        }
        return std::nullopt;
    } else {
        return convert<Param>(tokens[I]);
    }
}

// Все аргументы сразу: index_sequence раскрывается в пакет 0..N-1. Порядок
// вычисления внутри фигурных скобок — слева направо, поэтому ошибки разбора
// сообщаются в порядке аргументов. [[maybe_unused]] — для команды без
// аргументов, как /rooms: пакет пуст, и tokens не используется.
template <class Params, std::size_t... I>
[[nodiscard]] Params convert_all([[maybe_unused]] std::span<const std::string_view> tokens,
                                 std::index_sequence<I...>) {
    return Params{argument_at<std::tuple_element_t<I, Params>, I>(tokens)...};
}

// Подсказка «/history <#room> [number] [nick]» — из типов параметров.
template <class Param>
[[nodiscard]] std::string usage_of() {
    if constexpr (is_optional_v<Param>) {
        return "[" + std::string(placeholder<typename Param::value_type>()) + "]";
    } else {
        return "<" + std::string(placeholder<Param>()) + ">";
    }
}

template <class... Params>
[[nodiscard]] std::string usage_for(std::string_view verb, std::type_identity<std::tuple<Params...>>) {
    std::string usage(verb);
    ((usage += ' ', usage += usage_of<Params>()), ...);
    return usage;
}

[[nodiscard]] inline std::string lower(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

}  // namespace detail

// Концепция обработчика команды. Что требуется, по порядку:
//   - HasSignature: сигнатуру можно разобрать (не обобщённая лямбда и не
//     перегруженный функтор);
//   - ReturnsResponse: результат приводится к Response;
//   - TakesContextFirst: первый параметр — Context&;
//   - ArgumentsSupported: остальные — типы, которые умеет convert;
//   - ArgumentOrderValid: обязательные, затем необязательные или Tail.
// Конъюнкция проверяется слева направо и останавливается на первом
// невыполненном требовании, поэтому следующие могут рассчитывать на
// предыдущие. Регистрация неподходящего объекта — ошибка в строке
// register_command с именем невыполненного требования;
// tests/compile_fail/bad_handler.cpp.
template <class F, class Context>
concept CommandHandler = detail::HasSignature<F> && detail::ReturnsResponse<F> &&
                         detail::TakesContextFirst<F, Context> && detail::ArgumentsSupported<F> &&
                         detail::ArgumentOrderValid<F>;

// Этап 6: реестр команд. Добавление команды — одна строка
// register_command: имя и обработчик. Число и типы аргументов, подсказка и
// разрезание строки выводятся из сигнатуры обработчика.
//
// Разбор и исполнение разделены. bind разбирает строку в аргументы нужных
// типов и возвращает связанную команду — здесь бросают ошибки разбора.
// Связанная команда исполняется над контекстом — здесь бросают ошибки
// хранилища. Аргументы — string_view внутрь строки, поэтому связанная
// команда живёт не дольше строки — правило этапа 2.
template <class Context>
class CommandRegistry {
public:
    class Bound {
    public:
        Bound(std::string context, std::function<Response(Context&)> action)
            : context_(std::move(context)), action_(std::move(action)) {}

        // Звёздочка этапа 3 переехала сюда: ошибка из глубины получает
        // звено контекста «while executing …». Единственный catch внутри
        // исполнения, и он не проглатывает ошибку, а бросает её дальше.
        Response operator()(Context& context) const {
            try {
                return action_(context);
            } catch (...) {
                std::throw_with_nested(ChatError("while executing " + context_));
            }
        }

    private:
        std::string context_;   // глагол и первый аргумент: «/msg #a,#b»
        std::function<Response(Context&)> action_;
    };

    template <CommandHandler<Context> F>
    CommandRegistry& register_command(std::string_view name, F handler) {
        using Params = typename detail::parts_of<F>::params;
        using Shape = typename detail::parts_of<F>::shape;

        std::string verb = detail::lower(name);
        std::string usage = "usage: " + detail::usage_for(verb, std::type_identity<Params>{});
        Binder binder = [handler = std::move(handler), usage = std::move(usage)](
                            std::string_view line) -> std::function<Response(Context&)> {
            // С Tail строка режется ровно на глагол, обязательные аргументы
            // и остаток; без него — на токены целиком.
            const Tokens tokens = Shape::has_tail ? tokenize(line, Shape::count + 1) : tokenize(line);
            const auto args = std::span<const std::string_view>(tokens).subspan(1);
            if (args.size() < Shape::required || args.size() > Shape::count) {
                throw ParseError(usage, std::string(line));
            }
            // Ошибка разбора аргумента — ParseError из convert с причиной и
            // неразобранным токеном. Реестр приклеивает к ней подсказку тем
            // же throw_with_nested, что и контекст исполнения: на границе
            // получится «not a number: x (usage: /react <#room> <number>
            // [number])».
            try {
                auto converted = detail::convert_all<Params>(
                    args, std::make_index_sequence<std::tuple_size_v<Params>>{});
                return [handler, converted = std::move(converted)](Context& context) {
                    return std::apply(
                        [&](const auto&... values) { return Response(handler(context, values...)); },
                        converted);
                };
            } catch (...) {
                std::throw_with_nested(ChatError(usage));
            }
        };
        const auto [it, inserted] = table_.try_emplace(std::move(verb), std::move(binder));
        if (!inserted) {
            throw std::logic_error("command registered twice: " + it->first);
        }
        return *this;
    }

    // Разбор строки целиком. Проверки, общие для всех команд, — длина и
    // кодировка строки — до поиска глагола.
    [[nodiscard]] Bound bind(std::string_view line) const {
        if (const auto problem = line_problem(line); problem) {
            throw ParseError(*problem, std::string(line));
        }
        // Сначала только глагол и остаток: как резать остаток, знает
        // команда.
        const Tokens head = tokenize(line, 2);
        if (head.empty()) {
            throw ParseError("empty command", std::string(line));
        }
        const auto it = table_.find(detail::lower(head.front()));
        if (it == table_.end()) {
            throw ParseError("unknown command: " + std::string(head.front()), std::string(line));
        }
        std::function<Response(Context&)> action = it->second(line);
        std::string context = it->first;
        if (const Tokens first = tokenize(line, 3); first.size() > 1) {
            context += ' ';
            context += first[1];
        }
        return Bound(std::move(context), std::move(action));
    }

    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> result;
        for (const auto& [name, binder] : table_) {
            result.push_back(name);
        }
        return result;
    }

private:
    using Binder = std::function<std::function<Response(Context&)>(std::string_view)>;

    std::map<std::string, Binder, std::less<>> table_;
};

}  // namespace chat
