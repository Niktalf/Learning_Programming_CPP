// Домашнее задание этапа 5: модуль. Автомат состояний задачи из state.hpp
// вынесен в именованный модуль taskq.state. Что получилось и что нет —
// docs/modules.md.
//
// Заголовок стандартной библиотеки подключается в глобальном фрагменте
// модуля: import std потребовал бы CMake 3.30 с экспериментальным флагом,
// а проект собирается CMake 3.28.
//
// Интерес этого модуля в том, что всё в нём — constexpr: перечисление,
// таблица переходов, static_assert. Модуль экспортирует их вместе с
// определениями, и потребитель может вычислять переходы при компиляции.

module;

#include <string_view>

export module taskq.state;

export namespace taskq::modules {

enum class State { Pending, Leased, Done, Failed };

// Единственное место, где решается, какой переход разрешён. Проверки не
// размазаны по командам: на этапе 11 переход должен стать атомарным, и
// сделать атомарным можно только то, что собрано в одной точке.
//
// Leased -> Pending — возврат аренды: воркер взял задачу и не подтвердил её.
// Сейчас его вызывает команда RELEASE, на этапе 11 — сборщик просроченных
// аренд. Это единственный обратный переход, и его чаще всего забывают.
constexpr bool allowed(State from, State to) noexcept {
    switch (from) {
        case State::Pending:
            return to == State::Leased;
        case State::Leased:
            return to == State::Done || to == State::Failed || to == State::Pending;
        case State::Done:
        case State::Failed:
            return false;
    }
    return false;
}

constexpr std::string_view name(State state) noexcept {
    switch (state) {
        case State::Pending:
            return "pending";
        case State::Leased:
            return "leased";
        case State::Done:
            return "done";
        case State::Failed:
            return "failed";
    }
    return "unknown";
}

// Таблица проверяется при компиляции: ошибка в ней не соберётся.
static_assert(allowed(State::Pending, State::Leased), "POLL leases a pending task");
static_assert(allowed(State::Leased, State::Pending), "RELEASE returns a lease");
static_assert(!allowed(State::Done, State::Done), "a finished task stays finished");
static_assert(!allowed(State::Pending, State::Done), "only a leased task can be acknowledged");

}  // namespace taskq::modules
