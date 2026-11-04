#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kvs/value.hpp"

using kvs::IntValue;
using kvs::ListValue;
using kvs::StringValue;
using kvs::Value;

TEST(Value, SpecialFunctionsAtCompileTime) {
    // Этап 4: правило нуля больше не выполняется само — копирование написано
    // руками через clone. Перемещение по-прежнему сгенерировано и не бросает.
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<Value>);
    EXPECT_TRUE(std::is_nothrow_move_assignable_v<Value>);
    EXPECT_TRUE(std::is_copy_constructible_v<Value>);
    EXPECT_TRUE(std::is_copy_assignable_v<Value>);
    EXPECT_FALSE(std::is_nothrow_copy_constructible_v<Value>);   // clone выделяет память
    EXPECT_TRUE(std::is_nothrow_destructible_v<Value>);
}

TEST(Value, StoresAndComparesText) {
    const Value a(std::string("hello"));
    const Value b(std::string("hello"));
    const Value c(std::string("world"));

    EXPECT_EQ(a.to_string(), std::string("hello"));
    EXPECT_EQ(a.size(), 5u);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Value, TypeIsDeterminedByTheClass) {
    const Value text(std::string("hello"));
    const Value number(std::int64_t{42});
    const Value list(std::make_unique<ListValue>());

    EXPECT_EQ(text.type_name(), std::string_view("string"));
    EXPECT_EQ(number.type_name(), std::string_view("int"));
    EXPECT_EQ(list.type_name(), std::string_view("list"));
    EXPECT_EQ(number.to_string(), std::string("42"));

    // as<T> — dynamic_cast: указатель или nullptr. Роль get_if у variant.
    EXPECT_EQ(text.as<IntValue>(), nullptr);
    ASSERT_NE(number.as<IntValue>(), nullptr);
    EXPECT_EQ(number.as<IntValue>()->data(), 42);

    // Разные классы не равны, даже если текст одинаковый.
    EXPECT_TRUE(Value(std::int64_t{1}) != Value(std::string("1")));
}

TEST(Value, CopyIsDeep) {
    auto items = std::make_unique<ListValue>();
    items->push_back("a");
    const Value original(std::move(items));

    Value copy = original;
    copy.as<ListValue>()->push_back("b");

    // Изменение копии не видно в оригинале: clone создал отдельный объект.
    EXPECT_EQ(original.as<ListValue>()->length(), 1u);
    EXPECT_EQ(copy.as<ListValue>()->length(), 2u);
    EXPECT_NE(original.as<ListValue>(), copy.as<ListValue>());
}

TEST(Value, CopyKeepsTheDynamicType) {
    const Value number(std::int64_t{7});
    const Value copy = number;
    // Копия через указатель на базу сохранила тип — ради этого и нужен clone.
    EXPECT_EQ(copy.type_name(), std::string_view("int"));
    EXPECT_TRUE(copy == number);
}

TEST(Value, MoveTransfersOwnershipWithoutCopy) {
    Value source(std::string("payload"));
    const StringValue* const object = source.as<StringValue>();
    const Value target{std::move(source)};

    // Тот же объект в куче — перемещение передало указатель, не копию.
    EXPECT_EQ(target.as<StringValue>(), object);
    EXPECT_EQ(target.to_string(), std::string("payload"));
}

TEST(Value, MovedFromValueIsUsable) {
    Value source(std::string("payload"));
    Value target{std::move(source)};

    // Перемещённое значение пусто, но методы без предусловий работают.
    EXPECT_EQ(source.size(), 0u);
    EXPECT_EQ(source.type_name(), std::string_view("none"));
    EXPECT_EQ(source.to_string(), std::string());

    // И ему можно присвоить новое значение.
    source = Value(std::int64_t{1});
    EXPECT_EQ(source.type_name(), std::string_view("int"));
    static_cast<void>(target);
}

TEST(Value, SelfAssignmentIsSafe) {
    Value value(std::string("same"));
    const Value& alias = value;
    value = alias;
    EXPECT_EQ(value.to_string(), std::string("same"));
}

TEST(Value, ListAccountsBytesAndLength) {
    ListValue list;
    list.push_back("ab");
    list.push_back("cde");
    EXPECT_EQ(list.length(), 2u);
    EXPECT_EQ(list.size(), 5u);   // объём для учёта памяти — сумма длин
    EXPECT_EQ(list.to_string(), std::string("[ab, cde]"));
}
