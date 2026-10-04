#include <gtest/gtest.h>

#include <string_view>

#include "state.hpp"

TEST(State, ForwardPathIsAllowed) {
    EXPECT_TRUE(allowed(State::Pending, State::Leased));
    EXPECT_TRUE(allowed(State::Leased, State::Done));
    EXPECT_TRUE(allowed(State::Leased, State::Failed));
}

TEST(State, LeaseCanBeReturned) {
    EXPECT_TRUE(allowed(State::Leased, State::Pending));
}

TEST(State, CannotAcknowledgeUnleasedTask) {
    EXPECT_FALSE(allowed(State::Pending, State::Done));
    EXPECT_FALSE(allowed(State::Pending, State::Failed));
}

TEST(State, TerminalStatesAreFinal) {
    for (const State to : {State::Pending, State::Leased, State::Done, State::Failed}) {
        EXPECT_FALSE(allowed(State::Done, to));
        EXPECT_FALSE(allowed(State::Failed, to));
    }
}

TEST(State, NamesAreStable) {
    EXPECT_EQ(name(State::Leased), std::string_view("leased"));
}
