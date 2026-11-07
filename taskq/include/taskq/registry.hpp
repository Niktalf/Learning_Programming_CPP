#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "taskq/convert.hpp"
#include "taskq/errors.hpp"
#include "taskq/function_traits.hpp"
#include "taskq/lexer.hpp"
#include "taskq/validate.hpp"

namespace taskq {

// Ответ клиенту. Неудача внутри ядра — исключение; ok == false появляется
// только на границе handle, где исключение превращается в ответ.
struct Response {
    bool ok = true;
    std::string text;
};

namespace detail {

// Чем параметр обработчика является для разбора строки.
enum class Slot { Required, Optional, Named, Tail, Tokens };

template <class Param>
[[nodiscard]] consteval Slot slot_of() noexcept {
    if constexpr (std::is_same_v<Param, Tail>) {
        return Slot::Tail;
    } else if constexpr (TokenList<Param>) {
        return Slot::Tokens;
    } else if constexpr (is_optional_v<Param>) {
        return Named<typename Param::value_type> ? Slot::Named : Slot::Optional;
    } else {
        return Slot::Required;
    }
}

template <class Param>
[[nodiscard]] consteval std::string_view prefix_of() noexcept {
    // Две вложенные проверки, а не одна через &&: в условии if constexpr
    // вычисляются обе части, и Param::value_type у не-optional не
    // существует — жёсткая ошибка, а не ложь.
    if constexpr (is_optional_v<Param>) {
        if constexpr (Named<typename Param::value_type>) {
            return Param::value_type::prefix;
        } else {
            return {};
        }
    } else {
        return {};
    }
}

// Форма списка параметров после контекста:
//
//     обязательные*  именованные*  (необязательные* | Tail | список токенов)
//
// Именованные — std::optional от типа с префиксом (prio=, limit=); их место
// в строке определяет префикс, поэтому они могут стоять и перед Tail. Обычные
// необязательные — по позиции, и после них Tail невозможен: если
// необязательный пропущен, его место занял бы текст. Проверяется при
// компиляции.
template <class... Params>
struct Shape {
    static constexpr std::size_t count = sizeof...(Params);
    static constexpr std::array<Slot, count> slots = {slot_of<Params>()...};
    static constexpr std::array<std::string_view, count> prefixes = {prefix_of<Params>()...};
    static constexpr std::array<std::string_view, count> names = {placeholder<Params>()...};

    static constexpr bool valid = [] {
        // Порядок групп: 0 — обязательные, 1 — именованные, 2 — хвост
        // (необязательные, Tail или список токенов). Группа не может убывать.
        int group = 0;
        bool tail_seen = false;      // Tail или список токенов
        bool optional_seen = false;  // обычный необязательный
        for (std::size_t i = 0; i < count; ++i) {
            const Slot slot = slots[i];
            const int current = slot == Slot::Required ? 0 : slot == Slot::Named ? 1 : 2;
            if (current < group || tail_seen) {
                return false;
            }
            if ((slot == Slot::Tail || slot == Slot::Tokens) && optional_seen) {
                return false;
            }
            optional_seen = optional_seen || slot == Slot::Optional;
            tail_seen = slot == Slot::Tail || slot == Slot::Tokens;
            group = current;
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

// Полный список параметров вызова. У метода первым идёт объект — ссылка на
// него, с const для константного метода; у функции и лямбды — их параметры
// как есть. Так метод Service::ack(TaskId) и функция ack(Service&, TaskId)
// для реестра одно и то же.
//
// Через частичную специализацию, а не std::conditional_t: conditional_t
// вычисляет обе ветки, и object& для свободной функции — ссылка на void,
// жёсткая ошибка. Специализация инстанцирует только подходящую.
template <class Object, class Arguments>
struct with_object {
    using type = decltype(std::tuple_cat(std::declval<std::tuple<Object&>>(), std::declval<Arguments>()));
};

template <class Arguments>
struct with_object<void, Arguments> {
    using type = Arguments;
};

template <class F, class Traits = function_traits<std::decay_t<F>>>
using call_arguments_of = typename with_object<typename Traits::object, typename Traits::arguments>::type;

template <class F>
using parts_of = HandlerParts<call_arguments_of<F>>;

// Требования по одному, каждое со своим именем: диагностика называет то,
// которое не выполнено, а не одно общее «не подходит».
template <class F>
concept HasSignature = requires { typename function_traits<std::decay_t<F>>::result; };

template <class F>
concept ReturnsResponse =
    std::convertible_to<typename function_traits<std::decay_t<F>>::result, Response>;

// Первый параметр — контекст: изменяемый у команд, которые меняют сервис, и
// константный у тех, что только читают. Константность обработчика видна в
// сигнатуре, и STATUS не может ничего изменить по ошибке.
template <class F, class Context>
concept TakesContextFirst =
    parts_of<F>::has_context && (std::same_as<typename parts_of<F>::first, Context&> ||
                                 std::same_as<typename parts_of<F>::first, const Context&>);

template <class F>
concept ArgumentsSupported = parts_of<F>::arguments_supported;

template <class F>
concept ArgumentOrderValid = parts_of<F>::shape::valid;

// Раскладка строки по параметрам: какой токен достаётся какому параметру.
// Это единственная часть разбора, которая работает с курсором, и она не
// знает типов — только форму. Преобразование по типам — потом, в
// convert_all.
template <class Shape>
struct Layout {
    std::array<std::optional<std::string_view>, Shape::count> tokens{};
    std::vector<std::string_view> rest;
};

template <class Shape>
[[nodiscard]] Layout<Shape> lay_out(TokenCursor& cursor, std::string_view line) {
    const auto missing = [line](std::size_t i) {
        return ParseError("missing <" + std::string(Shape::names[i]) + ">", std::string(line));
    };
    Layout<Shape> layout;
    for (std::size_t i = 0; i < Shape::count; ++i) {
        switch (Shape::slots[i]) {
            case Slot::Required: {
                const auto token = cursor.next();
                if (!token) {
                    throw missing(i);
                }
                layout.tokens[i] = token;
                break;
            }
            case Slot::Optional:
                layout.tokens[i] = cursor.next();
                break;
            case Slot::Named: {
                // Группа именованных подряд: каждый следующий токен с
                // подходящим префиксом занимает свой параметр, в любом
                // порядке. Первый неподходящий токен — конец группы.
                std::size_t end = i;
                while (end < Shape::count && Shape::slots[end] == Slot::Named) {
                    ++end;
                }
                while (const auto token = cursor.peek()) {
                    std::size_t matched = end;
                    for (std::size_t k = i; k < end; ++k) {
                        if (!layout.tokens[k] && token->starts_with(Shape::prefixes[k])) {
                            matched = k;
                            break;
                        }
                    }
                    if (matched == end) {
                        break;
                    }
                    layout.tokens[matched] = token->substr(Shape::prefixes[matched].size());
                    static_cast<void>(cursor.next());
                }
                i = end - 1;
                break;
            }
            case Slot::Tail: {
                const std::string_view tail = cursor.rest();
                if (tail.empty()) {
                    throw missing(i);
                }
                layout.tokens[i] = tail;
                break;
            }
            case Slot::Tokens:
                while (const auto token = cursor.next()) {
                    layout.rest.push_back(*token);
                }
                break;
        }
    }
    if (const auto extra = cursor.next(); extra) {
        throw ParseError("unexpected argument: " + std::string(*extra), std::string(*extra));
    }
    return layout;
}

// Аргумент номер I из раскладки.
template <class Param, std::size_t I, class Shape>
[[nodiscard]] Param argument_at(Layout<Shape>& layout) {
    if constexpr (TokenList<Param>) {
        return Param::from_tokens(std::move(layout.rest));
    } else if constexpr (std::is_same_v<Param, Tail>) {
        return Tail{*layout.tokens[I]};
    } else if constexpr (is_optional_v<Param>) {
        if (layout.tokens[I]) {
            return convert<typename Param::value_type>(*layout.tokens[I]);
        }
        return std::nullopt;
    } else {
        return convert<Param>(*layout.tokens[I]);
    }
}

// Все аргументы сразу: index_sequence раскрывается в пакет 0..N-1. Порядок
// вычисления внутри фигурных скобок — слева направо, поэтому ошибки разбора
// сообщаются в порядке аргументов.
template <class Params, class Shape, std::size_t... I>
[[nodiscard]] Params convert_all([[maybe_unused]] Layout<Shape>& layout, std::index_sequence<I...>) {
    return Params{argument_at<std::tuple_element_t<I, Params>, I>(layout)...};
}

// Подсказка «SUBMIT <queue> [prio=0..9] <body...>» — из типов параметров.
template <class Param>
[[nodiscard]] std::string usage_of() {
    if constexpr (is_optional_v<Param>) {
        return "[" + std::string(placeholder<typename Param::value_type>()) + "]";
    } else if constexpr (TokenList<Param>) {
        return std::string(placeholder<Param>());
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

[[nodiscard]] inline std::string upper(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return result;
}

}  // namespace detail

// Концепция обработчика команды. Что требуется, по порядку:
//   - HasSignature: сигнатуру можно разобрать (не обобщённая лямбда и не
//     перегруженный функтор);
//   - ReturnsResponse: результат приводится к Response;
//   - TakesContextFirst: первый параметр — Context& или const Context&; у
//     метода это сам объект;
//   - ArgumentsSupported: остальные — типы, которые умеет convert;
//   - ArgumentOrderValid: обязательные, именованные, затем необязательные,
//     Tail или список токенов (TokenList).
// Конъюнкция проверяется слева направо и останавливается на первом
// невыполненном требовании. Регистрация неподходящего объекта — ошибка в
// строке register_command с именем невыполненного требования;
// tests/compile_fail/bad_handler.cpp.
template <class F, class Context>
concept CommandHandler = detail::HasSignature<F> && detail::ReturnsResponse<F> &&
                         detail::TakesContextFirst<F, Context> && detail::ArgumentsSupported<F> &&
                         detail::ArgumentOrderValid<F>;

// Этап 6: реестр команд. Добавление команды — одна строка
// register_command: имя и обработчик. Число, порядок и типы аргументов,
// подсказка и место хвоста строки выводятся из сигнатуры обработчика.
//
// Разбор и исполнение разделены. bind разбирает строку и возвращает
// связанную команду — здесь бросают ошибки разбора. Связанная команда
// исполняется над контекстом — здесь бросают ошибки сервиса. Аргументы —
// string_view внутрь строки, поэтому связанная команда живёт не дольше
// строки — правило этапа 2.
template <class Context>
class CommandRegistry {
public:
    class Bound {
    public:
        Bound(std::string context, bool reads_only, std::function<Response(Context&)> action)
            : context_(std::move(context)), reads_only_(reads_only), action_(std::move(action)) {}

        // Звёздочка этапа 3 переехала сюда: ошибка из глубины получает
        // звено контекста «while executing …» и летит дальше.
        Response operator()(Context& context) const {
            try {
                return action_(context);
            } catch (...) {
                std::throw_with_nested(TaskQError("while executing " + context_));
            }
        }

        // Обработчик принимает const Context& — команда только читает.
        [[nodiscard]] bool reads_only() const noexcept { return reads_only_; }

    private:
        std::string context_;   // глагол и первый аргумент: «ACK 7»
        bool reads_only_;
        std::function<Response(Context&)> action_;
    };

    template <CommandHandler<Context> F>
    CommandRegistry& register_command(std::string_view name, F handler) {
        using Parts = detail::parts_of<F>;
        using Params = typename Parts::params;
        using Shape = typename Parts::shape;
        constexpr bool reads_only = std::is_same_v<typename Parts::first, const Context&>;

        std::string verb = detail::upper(name);
        std::string usage = "usage: " + detail::usage_for(verb, std::type_identity<Params>{});
        Binder binder = [handler = std::move(handler), usage = std::move(usage)](
                            TokenCursor& cursor,
                            std::string_view line) -> std::function<Response(Context&)> {
            // Ошибка раскладки или аргумента — ParseError с причиной.
            // Реестр приклеивает к ней подсказку тем же throw_with_nested,
            // что и контекст исполнения: на границе получится «task id must
            // be a positive number: sleeping (usage: ACK <id>)».
            try {
                auto layout = detail::lay_out<Shape>(cursor, line);
                auto converted = detail::convert_all<Params>(
                    layout, std::make_index_sequence<std::tuple_size_v<Params>>{});
                return [handler, converted = std::move(converted)](Context& context) {
                    return std::apply(
                        [&](const auto&... values) {
                            // std::invoke одинаково вызывает функцию и метод:
                            // для метода context становится объектом.
                            return Response(std::invoke(handler, context, values...));
                        },
                        converted);
                };
            } catch (...) {
                std::throw_with_nested(TaskQError(usage));
            }
        };
        const auto [it, inserted] = table_.try_emplace(std::move(verb), Entry{reads_only, std::move(binder)});
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
        TokenCursor cursor(line);
        const auto verb = cursor.next();
        if (!verb) {
            throw ParseError("empty command", std::string(line));
        }
        const auto it = table_.find(detail::upper(*verb));
        if (it == table_.end()) {
            throw ParseError("unknown command: " + std::string(*verb), std::string(line));
        }
        std::string context = it->first;
        if (const auto first = TokenCursor(cursor).peek(); first) {
            context += ' ';
            context += *first;
        }
        return Bound(std::move(context), it->second.reads_only, it->second.binder(cursor, line));
    }

    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> result;
        for (const auto& [name, entry] : table_) {
            result.push_back(name);
        }
        return result;
    }

private:
    using Binder =
        std::function<std::function<Response(Context&)>(TokenCursor&, std::string_view)>;

    struct Entry {
        bool reads_only;
        Binder binder;
    };

    std::map<std::string, Entry, std::less<>> table_;
};

}  // namespace taskq
