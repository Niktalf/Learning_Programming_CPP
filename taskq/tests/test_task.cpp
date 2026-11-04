#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "taskq/task.hpp"

using taskq::NumericPayload;
using taskq::Task;

TEST(Task, SpecialFunctionsAtCompileTime) {
    // Этап 4: правило нуля больше не выполняется само — копирование написано
    // руками через clone. Перемещение по-прежнему сгенерировано и не бросает.
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<Task>);
    EXPECT_TRUE(std::is_nothrow_move_assignable_v<Task>);
    EXPECT_TRUE(std::is_copy_constructible_v<Task>);
    EXPECT_TRUE(std::is_copy_assignable_v<Task>);
    EXPECT_FALSE(std::is_nothrow_copy_constructible_v<Task>);   // clone выделяет память
    EXPECT_TRUE(std::is_nothrow_destructible_v<Task>);
}

TEST(Task, StoresFields) {
    const Task a(1, "make all", 5);
    const Task b(1, "make all", 5);
    const Task c(2, "make all", 5);

    EXPECT_EQ(a.id(), 1u);
    EXPECT_EQ(a.to_string(), std::string("make all"));
    EXPECT_EQ(a.priority(), 5);
    EXPECT_EQ(a.size(), 8u);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Task, KindIsDeterminedByTheClass) {
    const Task text(1, "make all");
    const Task number(2, std::make_unique<NumericPayload>(7));

    EXPECT_EQ(text.type_name(), std::string_view("text"));
    EXPECT_EQ(number.type_name(), std::string_view("number"));
    // as<T> — dynamic_cast: указатель или nullptr. Роль get_if у variant.
    EXPECT_EQ(text.as<NumericPayload>(), nullptr);
    ASSERT_NE(number.as<NumericPayload>(), nullptr);
    EXPECT_EQ(number.as<NumericPayload>()->value(), 7);
}

TEST(Task, CopyIsDeep) {
    const Task original(1, std::make_unique<NumericPayload>(7));
    Task copy = original;
    copy.as<NumericPayload>()->set_value(42);

    // Изменение копии не видно в оригинале: clone создал отдельный объект.
    EXPECT_EQ(original.as<NumericPayload>()->value(), 7);
    EXPECT_EQ(copy.as<NumericPayload>()->value(), 42);
}

TEST(Task, MoveTransfersThePayloadWithoutCopy) {
    Task source(1, "payload", 3);
    const auto* const payload = source.as<taskq::TextPayload>();
    const Task target{std::move(source)};

    EXPECT_EQ(target.as<taskq::TextPayload>(), payload);   // тот же объект в куче
    // Перемещённая задача пуста, но методы без предусловий работают.
    EXPECT_EQ(source.size(), 0u);
    EXPECT_EQ(source.type_name(), std::string_view("none"));
}

TEST(Task, EmptyTaskIsAPlaceToMoveInto) {
    // POLL готовит пустую задачу заранее и потом перемещает в неё настоящую.
    EXPECT_TRUE(std::is_nothrow_default_constructible_v<Task>);
    Task slot;
    slot = Task(5, "work");
    EXPECT_EQ(slot.to_string(), std::string("work"));
}
