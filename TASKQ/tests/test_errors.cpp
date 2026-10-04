#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>

#include "errors.hpp"

TEST(Errors, HierarchyIsRootedAtTaskQError) {
    EXPECT_TRUE((std::is_base_of_v<std::runtime_error, TaskQError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, ParseError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, UnknownQueueError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, UnknownTaskError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, InvalidTransitionError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, TypeMismatchError>));
}

TEST(Errors, TransitionErrorCarriesBothStates) {
    const InvalidTransitionError e(7, State::Done, State::Leased);

    EXPECT_EQ(e.id(), 7u);
    EXPECT_TRUE(e.from() == State::Done);
    EXPECT_TRUE(e.to() == State::Leased);
}

TEST(Errors, CatchingByBaseReferenceKeepsDerivedType) {
    try {
        throw UnknownQueueError("build");
    } catch (const TaskQError& e) {
        const auto* derived = dynamic_cast<const UnknownQueueError*>(&e);

        ASSERT_NE(derived, nullptr);
        EXPECT_EQ(derived->queue(), std::string("build"));
    }
}
