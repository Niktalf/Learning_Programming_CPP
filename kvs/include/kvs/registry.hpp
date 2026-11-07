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

#include "kvs/convert.hpp"
#include "kvs/errors.hpp"
#include "kvs/function_traits.hpp"

namespace kvs {

// Результат успешной команды. Неудача — исключение, и ok == false выставляет
// только граница Server::handle.
struct Response {
    bool ok = true;
    std::string text;
};

// Изменяет ли команда хранилище. Только такие команды пишутся в журнал.
enum class Effect { Reads, Writes };

namespace detail {

// Форма списка параметров обработчика после контекста: сначала обязательные,
// затем необязательные (std::optional), Rest — только последним и только без
// необязательных перед ним. Проверяется при компиляции.
template <class... Params>
struct Shape {
    static constexpr std::array<bool, sizeof...(Params)> optional = {is_optional_v<Params>...};
    static constexpr std::array<bool, sizeof...(Params)> rest = {std::is_same_v<Params, Rest>...};

    static constexpr std::size_t required = [] {
        std::size_t count = 0;
        for (std::size_t i = 0; i < sizeof...(Params); ++i) {
            if (optional[i] || rest[i]) {
                break;
            }
            ++count;
        }
        return count;
    }();

    static constexpr bool has_rest = sizeof...(Params) > 0 && rest[sizeof...(Params) - 1];
    static constexpr std::size_t maximum = has_rest ? static_cast<std::size_t>(-1) : sizeof...(Params);

    // Корректна ли форма: после первого необязательного — только
    // необязательные, Rest — только последним и только после обязательных.
    static constexpr bool valid = [] {
        bool seen_optional = false;
        for (std::size_t i = 0; i < sizeof...(Params); ++i) {
            if (rest[i] && (i + 1 != sizeof...(Params) || seen_optional)) {
                return false;
            }
            if (optional[i]) {
                seen_optional = true;
            } else if (seen_optional && !rest[i]) {
                return false;
            }
        }
        return true;
    }();
};

// Разбор сигнатуры обработчика: первый параметр — контекст, остальные —
// аргументы команды.
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

// Аргумент номер I из токенов: Rest забирает все оставшиеся, optional —
// токен, если он есть, обычный тип — свой токен.
template <class Param, std::size_t I>
[[nodiscard]] Param argument_at(std::span<const std::string_view> tokens) {
    if constexpr (std::is_same_v<Param, Rest>) {
        return Rest{tokens.subspan(I)};
    } else if constexpr (is_optional_v<Param>) {
        if (I < tokens.size()) {
            return convert<typename Param::value_type>(tokens[I]);
        }
        return std::nullopt;
    } else {
        return convert<Param>(tokens[I]);
    }
}

// Все аргументы сразу. index_sequence раскрывается в пакет индексов 0..N-1,
// и для каждого вызывается argument_at с нужным типом. Порядок вычисления
// внутри фигурных скобок задан стандартом — слева направо, — поэтому ошибки
// разбора сообщаются в порядке аргументов.
//
// [[maybe_unused]] — для обработчика без аргументов: пакет пуст, и tokens
// не используется.
template <class Params, std::size_t... I>
[[nodiscard]] Params convert_all([[maybe_unused]] std::span<const std::string_view> tokens,
                                 std::index_sequence<I...>) {
    return Params{argument_at<std::tuple_element_t<I, Params>, I>(tokens)...};
}

}  // namespace detail

// Концепция обработчика команды. Что требуется, по порядку:
//   - HasSignature: сигнатуру можно разобрать (не обобщённая лямбда и не
//     перегруженный функтор);
//   - ReturnsResponse: результат приводится к Response;
//   - TakesContextFirst: первый параметр — Context&;
//   - ArgumentsSupported: остальные — типы, которые умеет convert;
//   - ArgumentOrderValid: обязательные, затем необязательные, Rest — последним.
// Конъюнкция концепций проверяется слева направо и останавливается на первой
// невыполненной, поэтому следующие могут рассчитывать на предыдущие: parts_of
// не вычисляется для обобщённой лямбды. Регистрация неподходящего объекта —
// ошибка в строке add с именем невыполненного требования;
// tests/compile_fail/bad_handler.cpp.
template <class F, class Context>
concept CommandHandler = detail::HasSignature<F> && detail::ReturnsResponse<F> &&
                         detail::TakesContextFirst<F, Context> && detail::ArgumentsSupported<F> &&
                         detail::ArgumentOrderValid<F>;

// Этап 6: реестр команд — точка расширения на весь оставшийся курс. На
// этапе 15 через него пойдут сетевые команды, на этапе 16 — маршруты HTTP.
// Добавление команды — одна строка add: имя, побочный эффект, обработчик.
//
// Разбор и исполнение разделены, как и раньше. bind разбирает токены в
// аргументы нужных типов и возвращает связанную команду — здесь бросает
// ParseError. Связанная команда исполняется над контекстом — здесь бросают
// ошибки хранилища. Между ними связанную команду можно сохранить: так её
// держит кэш запросов этапа 4.
template <class Context>
class CommandRegistry {
public:
    // Связанная команда: имя, эффект и действие с уже разобранными
    // аргументами. Аргументы — string_view внутрь исходной строки, поэтому
    // связанная команда живёт не дольше строки — правило этапа 2.
    class Bound {
    public:
        Bound(std::string_view name, Effect effect, std::string context,
              std::function<Response(Context&)> action)
            : name_(name), effect_(effect), context_(std::move(context)), action_(std::move(action)) {}

        [[nodiscard]] std::string_view name() const noexcept { return name_; }
        [[nodiscard]] bool writes() const noexcept { return effect_ == Effect::Writes; }

        // Звёздочка этапа 3 переехала сюда: ошибка из глубины получает
        // звено контекста «while executing …». Это единственный catch внутри
        // ядра, и он не проглатывает ошибку, а бросает её дальше.
        Response operator()(Context& context) const {
            try {
                return action_(context);
            } catch (...) {
                std::throw_with_nested(KvError("while executing " + context_));
            }
        }

    private:
        std::string_view name_;
        Effect effect_;
        std::string context_;   // глагол и первый аргумент — для цепочки контекста
        std::function<Response(Context&)> action_;
    };

    // Регистрация. Обработчик проверяется концепцией CommandHandler; форма
    // параметров определяет арность, и проверять её в обработчике не нужно.
    template <CommandHandler<Context> F>
    CommandRegistry& add(std::string name, Effect effect, F handler) {
        using Params = typename detail::parts_of<F>::params;
        using Shape = typename detail::parts_of<F>::shape;
        constexpr std::size_t count = std::tuple_size_v<Params>;

        std::string usage = expects(name, Shape::required, Shape::maximum);
        Binder binder = [handler = std::move(handler), usage = std::move(usage)](
                            std::span<const std::string_view> args,
                            std::string_view line) -> std::function<Response(Context&)> {
            if (args.size() < Shape::required || args.size() > Shape::maximum) {
                throw ParseError(usage, std::string(line));
            }
            auto converted = detail::convert_all<Params>(args, std::make_index_sequence<count>{});
            return [handler, converted = std::move(converted)](Context& context) {
                return std::apply(
                    [&](const auto&... values) { return Response(handler(context, values...)); },
                    converted);
            };
        };
        const auto [it, inserted] = table_.try_emplace(upper(name), Entry{effect, std::move(binder)});
        if (!inserted) {
            throw std::logic_error("command registered twice: " + name);
        }
        return *this;
    }

    // Разбор: tokens[0] — глагол, без учёта регистра, остальное — аргументы.
    // line — исходная строка, для сообщения об ошибке.
    [[nodiscard]] Bound bind(std::span<const std::string_view> tokens, std::string_view line) const {
        if (tokens.empty()) {
            throw ParseError("empty command", std::string(line));
        }
        const auto it = table_.find(upper(tokens.front()));
        if (it == table_.end()) {
            throw ParseError("unknown command: " + std::string(tokens.front()), std::string(line));
        }
        // Ошибка разбора аргумента — ParseError из convert; её input — сам
        // неразобранный токен, а не вся строка: так точнее видно, что не так.
        const auto args = tokens.subspan(1);
        std::string context = it->first;
        if (!args.empty()) {
            context += ' ';
            context += args.front();
        }
        return Bound(it->first, it->second.effect, std::move(context), it->second.binder(args, line));
    }

    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> result;
        for (const auto& [name, entry] : table_) {
            result.push_back(name);
        }
        return result;
    }

private:
    using Binder = std::function<std::function<Response(Context&)>(std::span<const std::string_view>,
                                                                   std::string_view)>;

    struct Entry {
        Effect effect;
        Binder binder;
    };

    [[nodiscard]] static std::string upper(std::string_view text) {
        std::string result(text);
        for (char& c : result) {
            if (c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }
        return result;
    }

    // «SET expects 2 arguments», «INCR expects 1 or 2 arguments»,
    // «MSET expects at least 2 arguments» — из формы параметров, а не руками.
    [[nodiscard]] static std::string expects(const std::string& name, std::size_t minimum,
                                             std::size_t maximum) {
        const std::string verb = upper(name);
        const auto plural = [](std::size_t n) { return n == 1 ? " argument" : " arguments"; };
        if (maximum == static_cast<std::size_t>(-1)) {
            return verb + " expects at least " + std::to_string(minimum) + plural(minimum);
        }
        if (minimum == maximum) {
            return verb + " expects " + std::to_string(minimum) + plural(minimum);
        }
        return verb + " expects " + std::to_string(minimum) + " to " + std::to_string(maximum) +
               " arguments";
    }

    std::map<std::string, Entry, std::less<>> table_;
};

}  // namespace kvs
