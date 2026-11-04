#include <gtest/gtest.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "taskq/errors.hpp"

using taskq::State;
using taskq::TaskQError;

TEST(Errors, HierarchyIsRootedAtTaskQError) {
    // Одна ветка catch (const TaskQError&) ловит любую ошибку проекта, а
    // catch (const std::runtime_error&) — любую ошибку вообще.
    EXPECT_TRUE((std::is_base_of_v<std::runtime_error, TaskQError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, taskq::ParseError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, taskq::UnknownQueueError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, taskq::UnknownTaskError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, taskq::InvalidTransitionError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, taskq::WrongStateError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, taskq::TypeMismatchError>));
    EXPECT_TRUE((std::is_base_of_v<TaskQError, taskq::OverflowError>));
}

TEST(Errors, ContextIsInFieldsNotOnlyInMessage) {
    // На этапе 16 код ответа выбирается по полям, а не по разбору what().
    const taskq::InvalidTransitionError transition(7, State::Done, State::Done);
    EXPECT_EQ(transition.id(), 7u);
    EXPECT_EQ(transition.from(), State::Done);
    EXPECT_EQ(transition.to(), State::Done);
    EXPECT_EQ(std::string(transition.what()), std::string("task 7: done -> done not allowed"));

    const taskq::UnknownQueueError queue("build");
    EXPECT_EQ(queue.queue(), std::string("build"));
}

TEST(Errors, CatchingByBaseReferenceKeepsDerivedType) {
    try {
        throw taskq::UnknownTaskError(42);
    } catch (const TaskQError& e) {
        // Поймали по ссылке на базу — динамический тип сохранился.
        const auto* const derived = dynamic_cast<const taskq::UnknownTaskError*>(&e);
        ASSERT_NE(derived, nullptr);
        EXPECT_EQ(derived->id(), 42u);
    }
}

// Звёздочка: цепочка из трёх звеньев разворачивается в причину и контекст.
TEST(Errors, NestedChainUnwindsToRootCause) {
    std::exception_ptr chain;
    try {
        try {
            try {
                throw taskq::UnknownQueueError("absent");
            } catch (...) {
                std::throw_with_nested(TaskQError("while submitting item 2"));
            }
        } catch (...) {
            std::throw_with_nested(TaskQError("while executing SUBMITBATCH of 3"));
        }
    } catch (...) {
        chain = std::current_exception();
    }

    try {
        std::rethrow_exception(taskq::root_cause(chain));
    } catch (const taskq::UnknownQueueError& e) {
        EXPECT_EQ(e.queue(), std::string("absent"));
    } catch (...) {
        ADD_FAILURE() << "root cause has the wrong type";
    }

    const std::vector<std::string> context = taskq::context_of(chain);
    ASSERT_EQ(context.size(), 2u);
    EXPECT_EQ(context[0], std::string("while executing SUBMITBATCH of 3"));
    EXPECT_EQ(context[1], std::string("while submitting item 2"));
}

TEST(Errors, PlainExceptionIsItsOwnRootCause) {
    const std::exception_ptr single = std::make_exception_ptr(taskq::ParseError("empty command", ""));
    EXPECT_EQ(taskq::root_cause(single), single);
    EXPECT_TRUE(taskq::context_of(single).empty());
}
