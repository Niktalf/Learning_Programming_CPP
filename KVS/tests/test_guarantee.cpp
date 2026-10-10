#include <gtest/gtest.h>

#include <new>
#include <string>
#include <utility>
#include <vector>

#include "store.hpp"

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

        static void countdown(const int new_countdown) {
            countdown_ = new_countdown;
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

    using TestStore = BasicStore<ThrowOnNthCopy>;
    using Pairs = std::vector<std::pair<std::string, ThrowOnNthCopy>>;
}  // namespace

TEST(Guarantee, MsetIsAllOrNothing) {
    TestStore store;
    store.set("a", ThrowOnNthCopy("old-a"));
    store.set("b", ThrowOnNthCopy("old-b"));

    const Pairs pairs = {{"a", ThrowOnNthCopy("new-a")},
                         {"c", ThrowOnNthCopy("new-c")},
                         {"d", ThrowOnNthCopy("new-d")},
                         {"e", ThrowOnNthCopy("new-e")}};

    ThrowOnNthCopy::countdown(5);
    bool thrown = false;
    try {
        store.mset(pairs);
    } catch (const std::bad_alloc&) {
        thrown = true;
    }
    ThrowOnNthCopy::countdown(-1);

    ASSERT_TRUE(thrown);

    EXPECT_EQ(store.size(), 2u);
    EXPECT_EQ(store.get("a")->text(), std::string("old-a"));
    EXPECT_EQ(store.get("b")->text(), std::string("old-b"));
    EXPECT_FALSE(store.contains("c"));
    EXPECT_FALSE(store.contains("d"));
}

TEST(Guarantee, MsetWithoutFailureWritesEverything) {
    TestStore store;
    store.set("a", ThrowOnNthCopy("old-a"));

    const Pairs pairs = {{"a", ThrowOnNthCopy("new-a")}, {"b", ThrowOnNthCopy("new-b")}};
    store.mset(pairs);

    EXPECT_EQ(store.size(), 2u);
    EXPECT_EQ(store.get("a")->text(), std::string("new-a"));
    EXPECT_EQ(store.get("b")->text(), std::string("new-b"));
}

TEST(Guarantee, SwapIsNoexcept) {
    TestStore a;
    TestStore b;
    EXPECT_TRUE(noexcept(swap(a, b)));
}
