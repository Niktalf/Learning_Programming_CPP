#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>

#include "ivalue.hpp"


TEST(Hierarchy, DestructorIsVirtual) {
    EXPECT_TRUE(std::has_virtual_destructor_v<IValue>);
}

TEST(Hierarchy, BaseCannotBeCopiedFromOutside) {
    EXPECT_FALSE(std::is_copy_constructible_v<IValue>);
    EXPECT_FALSE(std::is_abstract_v<StringValue>);
    EXPECT_TRUE(std::is_abstract_v<IValue>);
}

TEST(Hierarchy, CloneKeepsDynamicType) {
    const std::unique_ptr<IValue> original = std::make_unique<IntValue>(7);
    const std::unique_ptr<IValue> copy = original->clone();

    ASSERT_NE(dynamic_cast<IntValue*>(copy.get()), nullptr);
    EXPECT_TRUE(copy->equals(*original));
    EXPECT_NE(copy.get(), original.get());
}

TEST(Hierarchy, DeletionThroughBasePointerRunsDerivedDestructor) {
    static int destroyed = 0;
    struct Probe final : IValue {
        ~Probe() override { ++destroyed; }
        std::string_view type_name() const noexcept override { return "probe"; }
        std::string to_string() const override { return {}; }
        std::size_t size() const noexcept override { return 0; }
        std::unique_ptr<IValue> clone() const override { return std::make_unique<Probe>(); }
        bool equals(const IValue&) const noexcept override { return false; }
    };

    destroyed = 0;
    {
        const std::unique_ptr<IValue> value = std::make_unique<Probe>();
    }

    EXPECT_EQ(destroyed, 1);
}

TEST(Hierarchy, ListGrows) {
    ListValue list;
    list.push_back("a");
    list.push_back("b");

    EXPECT_EQ(list.size(), 2u);
    EXPECT_EQ(list.to_string(), std::string("[a, b]"));
}
