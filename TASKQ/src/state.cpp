#include "state.hpp"

static_assert(allowed(State::Pending, State::Leased));
static_assert(allowed(State::Leased, State::Done));
static_assert(allowed(State::Leased, State::Failed));
static_assert(allowed(State::Leased, State::Pending), "lease refund must be allowed");
static_assert(!allowed(State::Pending, State::Done), "an unbiased task cannot be confirmed");
static_assert(!allowed(State::Done, State::Leased), "you can't take the completed task");
static_assert(!allowed(State::Failed, State::Pending), "failed task is not returned");

std::string_view name(const State state) noexcept {
    switch (state) {
        case State::Pending: return "pending";
        case State::Leased:  return "leased";
        case State::Done:    return "done";
        case State::Failed:  return "failed";
    }
    return "unknown";
}
