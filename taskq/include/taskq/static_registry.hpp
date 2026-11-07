#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include "taskq/registry.hpp"

namespace taskq {

// Звёздочка этапа 6: имя команды — параметр шаблона, и повтор имени —
// ошибка компиляции, а не logic_error при запуске.
//
// Строковый литерал параметром шаблона быть не может: у него нет
// структурного типа. FixedString — обёртка над массивом символов, которая
// таким типом является (C++20, P0732): все поля открыты, сравнение —
// побайтовое. Конструктор из литерала не explicit, и вывод аргумента шаблона
// класса берёт N из длины литерала: Command<"ACK", ...> работает без <4>.
template <std::size_t N>
struct FixedString {
    char text[N]{};

    constexpr FixedString(const char (&literal)[N]) noexcept {   // неявный нарочно
        std::copy_n(literal, N, text);
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept { return {text, N - 1}; }
};

// Описание команды целиком в типе: имя и обработчик. Обработчик — указатель
// на функцию или на метод как параметр шаблона (auto, C++17).
template <FixedString Name, auto Handler>
struct Command {
    static constexpr std::string_view name = Name.view();
    static constexpr auto handler = Handler;
};

namespace detail {

[[nodiscard]] constexpr char upper_ascii(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] constexpr bool same_verb(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, {}, upper_ascii, upper_ascii);
}

// Все ли имена различны без учёта регистра. Квадратичный перебор — при
// компиляции и над десятком имён это ничего не стоит.
template <std::size_t N>
[[nodiscard]] constexpr bool distinct_verbs(const std::array<std::string_view, N>& names) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (same_verb(names[i], names[j])) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace detail

// Реестр, состав которого известен при компиляции. Повтор имени —
// static_assert с понятным текстом. Разбор и исполнение — те же, что у
// CommandRegistry: он строится внутри из пакета команд, поэтому поведение
// двух реестров одинаково по построению.
template <class Context, class... Commands>
class StaticRegistry {
    static constexpr std::array<std::string_view, sizeof...(Commands)> names_ = {Commands::name...};
    static_assert(detail::distinct_verbs(names_), "StaticRegistry: duplicate command name");
    static_assert((CommandHandler<decltype(Commands::handler), Context> && ...),
                  "StaticRegistry: handler does not satisfy CommandHandler");

public:
    StaticRegistry() { (table_.register_command(Commands::name, Commands::handler), ...); }

    [[nodiscard]] typename CommandRegistry<Context>::Bound bind(std::string_view line) const {
        return table_.bind(line);
    }

    // Есть ли команда — вопрос, на который реестр отвечает при компиляции.
    [[nodiscard]] static constexpr bool contains(std::string_view verb) noexcept {
        return std::ranges::any_of(names_, [verb](std::string_view name) {
            return detail::same_verb(name, verb);
        });
    }

    [[nodiscard]] static constexpr std::size_t size() noexcept { return sizeof...(Commands); }

private:
    CommandRegistry<Context> table_;
};

}  // namespace taskq
