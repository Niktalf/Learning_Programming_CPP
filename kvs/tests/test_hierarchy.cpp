// Иерархия IValue: виртуальный деструктор, clone, запрет обрезки.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include "kvs/ivalue.hpp"
#include "kvs/value.hpp"

namespace {

int& destroyed() {
    static int count = 0;
    return count;
}

// Наследник, который считает свои разрушения. Через него видно, что
// unique_ptr<IValue> вызывает деструктор наследника, а не только базы.
class CountingValue final : public kvs::IValue {
public:
    CountingValue() = default;
    CountingValue(const CountingValue&) = default;
    CountingValue& operator=(const CountingValue&) = delete;
    ~CountingValue() override { ++destroyed(); }

    [[nodiscard]] std::string_view type_name() const noexcept override { return "counting"; }
    [[nodiscard]] std::string to_string() const override { return "counting"; }
    [[nodiscard]] std::size_t size() const noexcept override { return 0; }
    [[nodiscard]] std::unique_ptr<kvs::IValue> clone() const override {
        return std::make_unique<CountingValue>(*this);
    }
    [[nodiscard]] bool equals(const kvs::IValue& other) const noexcept override {
        return dynamic_cast<const CountingValue*>(&other) != nullptr;
    }
};

}  // namespace

TEST(Hierarchy, DestructorIsVirtual) {
    // Без virtual удаление через указатель на базу — неопределённое
    // поведение, и unique_ptr от него не защищает.
    EXPECT_TRUE(std::has_virtual_destructor_v<kvs::IValue>);
    EXPECT_TRUE(std::is_abstract_v<kvs::IValue>);
}

TEST(Hierarchy, DeletingThroughBaseRunsDerivedDestructor) {
    destroyed() = 0;
    {
        const std::unique_ptr<kvs::IValue> value = std::make_unique<CountingValue>();
        static_cast<void>(value);
    }
    EXPECT_EQ(destroyed(), 1);
}

TEST(Hierarchy, CloneCreatesTheSameDynamicType) {
    destroyed() = 0;
    {
        const std::unique_ptr<kvs::IValue> original = std::make_unique<CountingValue>();
        const std::unique_ptr<kvs::IValue> copy = original->clone();
        EXPECT_NE(copy.get(), original.get());
        EXPECT_NE(dynamic_cast<const CountingValue*>(copy.get()), nullptr);
        EXPECT_TRUE(copy->equals(*original));
    }
    EXPECT_EQ(destroyed(), 2);
}

TEST(Hierarchy, BaseCannotBeCopiedFromOutside) {
    // Копирование базы защищено: копия IValue по значению обрезала бы
    // объект до базы. Снаружи копировать можно только через clone.
    EXPECT_FALSE(std::is_copy_constructible_v<kvs::IValue>);
    EXPECT_FALSE(std::is_copy_assignable_v<kvs::IValue>);
}

TEST(Hierarchy, CustomValueWorksInsideValue) {
    // Новый тип добавляется новым классом — ни хранилище, ни Value не меняются.
    const kvs::Value value(std::make_unique<CountingValue>());
    const kvs::Value copy = value;
    EXPECT_EQ(copy.type_name(), std::string_view("counting"));
}
