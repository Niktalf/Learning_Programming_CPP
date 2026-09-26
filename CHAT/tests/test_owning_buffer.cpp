#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

#include "owning_buffer.hpp"


TEST(OwningBuffer, CopyIsIndependent) {
    OwningBuffer original{std::string("payload")};
    OwningBuffer copy{original};

    ASSERT_EQ(copy.size(), original.size());
    EXPECT_EQ(copy.str(), std::string("payload"));
    EXPECT_NE(copy.data(), original.data());
}

TEST(OwningBuffer, MoveTransfersOwnership) {
    OwningBuffer source{std::string("payload")};
    const char* const address = source.data();

    OwningBuffer target{std::move(source)};

    EXPECT_EQ(target.data(), address);
    EXPECT_EQ(source.data(), nullptr);
    EXPECT_EQ(source.size(), 0u);
}

TEST(OwningBuffer, SelfAssignmentIsSafe) {
    OwningBuffer buffer{std::string("payload")};

    OwningBuffer& alias = buffer;
    buffer = alias;

    EXPECT_EQ(buffer.size(), 7u);
    EXPECT_EQ(buffer.str(), std::string("payload"));
}

TEST(OwningBuffer, AssignmentReplacesContent) {
    OwningBuffer target{std::string("old")};
    const OwningBuffer source{std::string("new-content")};

    target = source;

    EXPECT_EQ(target.str(), std::string("new-content"));
    EXPECT_NE(target.data(), source.data());
}

TEST(OwningBuffer, MoveOperationsAreNoexcept) {
    EXPECT_TRUE(std::is_nothrow_move_constructible<OwningBuffer>::value);
    EXPECT_TRUE(std::is_nothrow_move_assignable<OwningBuffer>::value);
}

TEST(OwningBuffer, EmptyBufferIsValid) {
    OwningBuffer empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.data(), nullptr);

    OwningBuffer copy{empty};
    EXPECT_TRUE(copy.empty());
}
