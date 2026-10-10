#pragma once

#include <string_view>

enum class State { Pending, Leased, Done, Failed };

[[nodiscard]] constexpr bool allowed(const State from, const State to) noexcept {
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

[[nodiscard]] std::string_view name(State state) noexcept;
