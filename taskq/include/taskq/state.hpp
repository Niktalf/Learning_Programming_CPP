#pragma once

#include <string_view>

namespace taskq {

// Состояние задачи. Этап 2 различал три случая неявно — по тому, в каком
// контейнере лежит задача. Теперь состояние — отдельное значение, а
// допустимые переходы собраны в одной функции.
//
//              POLL              ACK
//   Pending ─────────▶ Leased ─────────▶ Done
//      ▲                 │  │
//      └─────────────────┘  └──────────▶ Failed
//          RELEASE                FAIL
//      (возврат аренды)
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

}  // namespace taskq
