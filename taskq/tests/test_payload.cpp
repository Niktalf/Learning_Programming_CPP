// Иерархия тел задачи: виртуальный деструктор, clone, запрет обрезки.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include "taskq/payload.hpp"

using taskq::IPayload;
using taskq::PayloadPtr;

namespace {

int& destroyed() {
    static int count = 0;
    return count;
}

class CountingPayload final : public IPayload {
public:
    CountingPayload() = default;
    CountingPayload(const CountingPayload&) = default;
    CountingPayload& operator=(const CountingPayload&) = delete;
    ~CountingPayload() override { ++destroyed(); }

    [[nodiscard]] std::string_view type_name() const noexcept override { return "counting"; }
    [[nodiscard]] std::string to_string() const override { return "counting"; }
    [[nodiscard]] std::size_t size() const noexcept override { return 0; }
    [[nodiscard]] PayloadPtr clone() const override { return std::make_unique<CountingPayload>(*this); }
    [[nodiscard]] bool equals(const IPayload& other) const noexcept override {
        return dynamic_cast<const CountingPayload*>(&other) != nullptr;
    }
};

}  // namespace

TEST(Payload, DestructorIsVirtual) {
    // Без virtual удаление через указатель на базу — неопределённое
    // поведение, и unique_ptr от него не защищает.
    EXPECT_TRUE(std::has_virtual_destructor_v<IPayload>);
    EXPECT_TRUE(std::is_abstract_v<IPayload>);
}

TEST(Payload, DeletingThroughBaseRunsDerivedDestructor) {
    destroyed() = 0;
    {
        const PayloadPtr payload = std::make_unique<CountingPayload>();
        static_cast<void>(payload);
    }
    EXPECT_EQ(destroyed(), 1);
}

TEST(Payload, CloneKeepsTheKind) {
    const PayloadPtr original = std::make_unique<taskq::BinaryPayload>(std::string("\xca\xfe"));
    const PayloadPtr copy = original->clone();
    EXPECT_NE(copy.get(), original.get());
    EXPECT_EQ(copy->type_name(), std::string_view("binary"));
    EXPECT_TRUE(copy->equals(*original));
    EXPECT_EQ(copy->to_string(), std::string("<2 bytes>"));
}

TEST(Payload, DifferentKindsAreNotEqual) {
    const taskq::TextPayload text("42");
    const taskq::NumericPayload number(42);
    EXPECT_FALSE(text.equals(number));
    EXPECT_FALSE(number.equals(text));
}

TEST(Payload, BaseCannotBeCopiedFromOutside) {
    EXPECT_FALSE(std::is_copy_assignable_v<IPayload>);
    EXPECT_FALSE(std::is_copy_constructible_v<PayloadPtr>);
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<PayloadPtr>);
}
