// Автомат состояний задачи проверяется целиком: двадцать пять пар, из них
// разрешены ровно пять (этап 6 добавил Cancelled и PURGE). Таблица в тесте записана независимо от switch
// в state.hpp — иначе тест проверял бы код сам на себя.

#include <gtest/gtest.h>

#include <array>
#include <utility>

#include "taskq/state.hpp"

using taskq::State;

TEST(State, ExactlyFiveTransitionsAreAllowed) {
    constexpr std::array<State, 5> states = {State::Pending, State::Leased, State::Done,
                                             State::Failed, State::Cancelled};
    constexpr std::array<std::pair<State, State>, 5> expected = {{
        {State::Pending, State::Leased},   // POLL
        {State::Pending, State::Cancelled},   // PURGE
        {State::Leased, State::Done},      // ACK
        {State::Leased, State::Failed},    // FAIL
        {State::Leased, State::Pending},   // RELEASE — возврат аренды
    }};
    for (const State from : states) {
        for (const State to : states) {
            bool listed = false;
            for (const auto& [a, b] : expected) {
                listed = listed || (a == from && b == to);
            }
            EXPECT_EQ(taskq::allowed(from, to), listed)
                << taskq::name(from) << " -> " << taskq::name(to);
        }
    }
}

TEST(State, LeaseCanBeReturned) {
    // Единственный обратный переход, его чаще всего забывают. Без него упавший
    // воркер уносил бы задачу навсегда, а сборщику этапа 11 было бы некуда
    // встроиться.
    EXPECT_TRUE(taskq::allowed(State::Leased, State::Pending));
}

TEST(State, FinishedStatesAreTerminal) {
    for (const State to :
         {State::Pending, State::Leased, State::Done, State::Failed, State::Cancelled}) {
        EXPECT_FALSE(taskq::allowed(State::Done, to));
        EXPECT_FALSE(taskq::allowed(State::Failed, to));
        EXPECT_FALSE(taskq::allowed(State::Cancelled, to));
    }
}
