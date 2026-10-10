#include <gtest/gtest.h>

#include <new>
#include <string>
#include <utility>
#include <vector>

#include "chat_store.hpp"

namespace {
    class ThrowOnNthCopy {
    public:
        ThrowOnNthCopy() = default;
        explicit ThrowOnNthCopy(std::string text) : text_(std::move(text)) {}

        ThrowOnNthCopy(const ThrowOnNthCopy& other) : text_(other.text_) { tick(); }
        ThrowOnNthCopy& operator=(const ThrowOnNthCopy& other) {
            tick();
            text_ = other.text_;
            return *this;
        }
        ThrowOnNthCopy(ThrowOnNthCopy&&) noexcept = default;
        ThrowOnNthCopy& operator=(ThrowOnNthCopy&&) noexcept = default;

        [[nodiscard]] const std::string& text() const noexcept { return text_; }

        static void countdown(const int countdown) noexcept {
            countdown_ = countdown;
        }

    private:
        static void tick() {
            if (countdown_ > 0 && --countdown_ == 0) {
                throw std::bad_alloc();
            }
        }
        std::string text_;

        static inline int countdown_ = -1;
    };

    using TestStore = BasicChatStore<ThrowOnNthCopy>;
}  // namespace

TEST(Guarantee, PostManyIsAllOrNothingOnCopyFailure) {
    TestStore store;
    store.create_room("#a");
    store.create_room("#b");
    store.create_room("#c");
    store.post("#a", ThrowOnNthCopy("old"));

    ThrowOnNthCopy::countdown(3);
    bool thrown = false;
    try {
        store.post_many({"#a", "#b", "#c"}, ThrowOnNthCopy("new"));
    } catch (const std::bad_alloc&) {
        thrown = true;
    }
    ThrowOnNthCopy::countdown(-1);

    ASSERT_TRUE(thrown);
    EXPECT_EQ(store.history("#a")->get().size(), 1u);   // только старое
    EXPECT_EQ(store.history("#b")->get().size(), 0u);
    EXPECT_EQ(store.history("#c")->get().size(), 0u);
}

TEST(Guarantee, PostManyIsAllOrNothingOnMissingRoom) {
    TestStore store;
    store.create_room("#a");
    store.create_room("#b");

    EXPECT_THROW(store.post_many({"#a", "#b", "#absent"}, ThrowOnNthCopy("new")),
                 UnknownRoomError);

    EXPECT_EQ(store.history("#a")->get().size(), 0u);
    EXPECT_EQ(store.history("#b")->get().size(), 0u);
}

TEST(Guarantee, PostManyWithoutFailureReachesEveryRoom) {
    TestStore store;
    store.create_room("#a");
    store.create_room("#b");

    store.post_many({"#a", "#b"}, ThrowOnNthCopy("hello"));

    EXPECT_EQ(store.history("#a")->get().size(), 1u);
    EXPECT_EQ(store.history("#b")->get().size(), 1u);
}

TEST(Guarantee, SwapIsNoexcept) {
    TestStore a;
    TestStore b;
    EXPECT_TRUE(noexcept(swap(a, b)));
}
