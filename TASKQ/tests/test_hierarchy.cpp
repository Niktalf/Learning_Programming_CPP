#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>

#include "ipayload.hpp"


TEST(Hierarchy, DestructorIsVirtual) {
    EXPECT_TRUE(std::has_virtual_destructor_v<IPayload>);
}

TEST(Hierarchy, BaseCannotBeCopiedFromOutside) {
    EXPECT_FALSE(std::is_copy_constructible_v<IPayload>);
    EXPECT_TRUE(std::is_abstract_v<IPayload>);
}

TEST(Hierarchy, CloneKeepsDynamicType) {
    const std::unique_ptr<IPayload> original = std::make_unique<NumericPayload>(5);
    const std::unique_ptr<IPayload> copy = original->clone();

    ASSERT_NE(dynamic_cast<NumericPayload*>(copy.get()), nullptr);
    EXPECT_TRUE(copy->equals(*original));
    EXPECT_NE(copy.get(), original.get());
}

TEST(Hierarchy, DeletionThroughBasePointerRunsDerivedDestructor) {
    static int destroyed = 0;
    struct Probe final : IPayload {
        ~Probe() override { ++destroyed; }
        [[nodiscard]] std::string_view type_name() const noexcept override { return "probe"; }
        [[nodiscard]] std::string to_string() const override { return {}; }
        [[nodiscard]] std::size_t size() const noexcept override { return 0; }
        [[nodiscard]] std::unique_ptr<IPayload> clone() const override { return std::make_unique<Probe>(); }
        [[nodiscard]] bool equals(const IPayload&) const noexcept override { return false; }
    };
    destroyed = 0;
    { const std::unique_ptr<IPayload> payload = std::make_unique<Probe>(); }

    EXPECT_EQ(destroyed, 1);
}
