// Этап 5: концепции проверяются не только на принятие, но и на отказ.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "taskq/concepts.hpp"
#include "taskq/task.hpp"
#include "taskq/task_key.hpp"
#include "taskq/task_store.hpp"
#include "tracked.hpp"

namespace {

struct WrongResult {
    [[nodiscard]] int to_bytes() const { return 0; }
    static WrongResult from_bytes(std::span<const std::byte>) { return {}; }
};

struct WrongFactory {
    [[nodiscard]] std::vector<std::byte> to_bytes() const { return {}; }
    static int from_bytes(std::span<const std::byte>) { return 0; }
};

// Ключ возвращает не TaskKey, а приоритет числом: проверка «метод key()
// есть» пропустила бы, проверка типа результата — нет.
struct KeyIsInt {
    [[nodiscard]] std::uint64_t id() const { return 0; }
    [[nodiscard]] int key() const { return 0; }
    void set_sequence(std::uint64_t) {}
};

// Без номера задачи хранилище не найдёт её по STATUS.
struct NoId {
    [[nodiscard]] taskq::TaskKey key() const { return {}; }
    void set_sequence(std::uint64_t) {}
};

}  // namespace

TEST(Concepts, NameLike) {
    EXPECT_TRUE(taskq::NameLike<std::string>);
    EXPECT_TRUE(taskq::NameLike<std::string_view>);
    EXPECT_TRUE(taskq::NameLike<decltype("build")>);
    EXPECT_FALSE(taskq::NameLike<int>);
    // Указатель без длины — нет: string_view из нулевого указателя —
    // неопределённое поведение. convertible_to<string_view> его бы пропустил.
    EXPECT_FALSE(taskq::NameLike<const char*>);
    EXPECT_FALSE(taskq::NameLike<std::nullptr_t>);
}

TEST(Concepts, StoreAcceptsEveryNameLike) {
    taskq::TaskStore store;
    store.create_queue("build");
    const std::string owned = "build";
    EXPECT_TRUE(store.contains("build"));
    EXPECT_TRUE(store.contains(owned));
    EXPECT_TRUE(store.contains(std::string_view(owned)));
}

TEST(Concepts, QueuedTaskChecksResultTypes) {
    EXPECT_TRUE(taskq::QueuedTask<taskq::Task>);
    EXPECT_TRUE(taskq::QueuedTask<taskq::testing::Tracked>);
    EXPECT_FALSE(taskq::QueuedTask<KeyIsInt>);
    EXPECT_FALSE(taskq::QueuedTask<NoId>);
    EXPECT_FALSE(taskq::QueuedTask<taskq::PayloadPtr>);
}

TEST(Concepts, SerializableChecksResultTypes) {
    EXPECT_TRUE(taskq::Serializable<taskq::TaskKey>);
    EXPECT_FALSE(taskq::Serializable<taskq::PlainTaskKey>);
    EXPECT_FALSE(taskq::Serializable<WrongResult>);
    EXPECT_FALSE(taskq::Serializable<WrongFactory>);
}

TEST(Concepts, TaskKeyRoundTrip) {
    const taskq::TaskKey key{7, 42, "ci"};
    const taskq::TaskKey restored = taskq::TaskKey::from_bytes(key.to_bytes());
    EXPECT_EQ(restored, key);
    EXPECT_EQ(restored.requester, std::string("ci"));

    std::vector<std::byte> bytes = key.to_bytes();
    bytes.pop_back();
    EXPECT_THROW(static_cast<void>(taskq::TaskKey::from_bytes(bytes)), taskq::ParseError);
}
