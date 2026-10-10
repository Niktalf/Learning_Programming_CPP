#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>

#include "imessage.hpp"

TEST(Hierarchy, DestructorIsVirtual) {
    EXPECT_TRUE(std::has_virtual_destructor_v<IMessage>);
}

TEST(Hierarchy, BaseCannotBeCopiedFromOutside) {
    EXPECT_FALSE(std::is_copy_constructible_v<IMessage>);
    EXPECT_TRUE(std::is_abstract_v<IMessage>);
}

TEST(Hierarchy, CloneKeepsDynamicType) {
    const std::unique_ptr<IMessage> original = std::make_unique<ReactionMessage>("+1", 2);
    const std::unique_ptr<IMessage> copy = original->clone();

    ASSERT_NE(dynamic_cast<ReactionMessage*>(copy.get()), nullptr);
    EXPECT_TRUE(copy->equals(*original));
    EXPECT_NE(copy.get(), original.get());
}

TEST(Hierarchy, DeletionThroughBasePointerRunsDerivedDestructor) {
    static int destroyed = 0;
    struct Probe final : IMessage {
        ~Probe() override { ++destroyed; }
        [[nodiscard]] std::string_view type_name() const noexcept override { return "probe"; }
        [[nodiscard]] std::string to_string() const override { return {}; }
        [[nodiscard]] std::size_t size() const noexcept override { return 0; }
        [[nodiscard]] std::unique_ptr<IMessage> clone() const override { return std::make_unique<Probe>(); }
        [[nodiscard]] bool equals(const IMessage&) const noexcept override { return false; }
    };
    destroyed = 0;
    { const std::unique_ptr<IMessage> message = std::make_unique<Probe>(); }

    EXPECT_EQ(destroyed, 1);
}
