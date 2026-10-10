#include <gtest/gtest.h>

#include <new>
#include <string>
#include <utility>
#include <vector>

#include "task_store.hpp"

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

        static void countdown(const int count) {
            countdown_ = count;
        }

    private:
        static void tick() {
            if (countdown_ > 0 && --countdown_ == 0) {
                throw std::bad_alloc();
            }
        }
        std::string text_;
        inline static int countdown_ = -1;
    };

    using TestStore = BasicTaskStore<ThrowOnNthCopy>;
    using Items = std::vector<std::pair<std::string, ThrowOnNthCopy>>;
}  // namespace

TEST(Guarantee, BatchIsAllOrNothingOnCopyFailure) {
    TestStore store;
    store.create_queue("build");
    store.create_queue("test");
    store.create_queue("deploy");
    store.submit("build", ThrowOnNthCopy("old"));

    const Items items = {{"build", ThrowOnNthCopy("a")},
                         {"test", ThrowOnNthCopy("b")},
                         {"deploy", ThrowOnNthCopy("c")}};

    ThrowOnNthCopy::countdown(3);
    bool thrown = false;
    try {
        store.submit_batch(items);
    } catch (const std::bad_alloc&) {
        thrown = true;
    }
    ThrowOnNthCopy::countdown(-1);

    ASSERT_TRUE(thrown);
    EXPECT_EQ(store.size("build"), 1u);
    EXPECT_EQ(store.size("test"), 0u);
    EXPECT_EQ(store.size("deploy"), 0u);
}

TEST(Guarantee, BatchIsAllOrNothingOnMissingQueue) {
    TestStore store;
    store.create_queue("build");
    store.create_queue("test");

    const Items items = {{"build", ThrowOnNthCopy("a")},
                         {"test", ThrowOnNthCopy("b")},
                         {"absent", ThrowOnNthCopy("c")}};

    EXPECT_THROW(store.submit_batch(items), UnknownQueueError);
    EXPECT_EQ(store.size("build"), 0u);
    EXPECT_EQ(store.size("test"), 0u);
}

TEST(Guarantee, SwapIsNoexcept) {
    TestStore a;
    TestStore b;

    EXPECT_TRUE(noexcept(swap(a, b)));
}
