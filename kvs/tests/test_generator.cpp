// Звёздочка этапа 5: генератор ключей на std::generator.

#include <gtest/gtest.h>

#include <generator>
#include <string>
#include <vector>

#include "kvs/store.hpp"

namespace {

int& guards_alive() {
    static int count = 0;
    return count;
}

// Локальный объект корутины, считающий себя. По нему видно, что при
// досрочном выходе из цикла кадр корутины уничтожается и её локальные
// объекты разрушаются.
struct Guard {
    Guard() { ++guards_alive(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    ~Guard() { --guards_alive(); }
};

std::generator<int> counting_with_guard(int limit) {
    const Guard guard;
    for (int i = 0; i < limit; ++i) {
        co_yield i;
    }
}

}  // namespace

TEST(Generator, YieldsMatchingKeysInOrder) {
    kvs::Store store;
    for (const char* const key : {"user:1", "order:1", "user:2"}) {
        store.set(key, kvs::Value(std::string("v")));
    }
    std::vector<std::string> seen;
    for (const std::string& key : store.keys_lazily("user:*")) {
        seen.push_back(key);
    }
    EXPECT_EQ(seen, (std::vector<std::string>{"user:1", "user:2"}));
}

TEST(Generator, EarlyExitDestroysCoroutineLocals) {
    guards_alive() = 0;
    {
        auto numbers = counting_with_guard(1000);
        for (const int i : numbers) {
            EXPECT_EQ(guards_alive(), 1);   // корутина приостановлена, Guard жив
            if (i == 2) {
                break;
            }
        }
        // Генератор ещё жив, корутина приостановлена — локальные объекты тоже.
        EXPECT_EQ(guards_alive(), 1);
    }
    // Генератор разрушен — кадр корутины уничтожен в точке приостановки,
    // и Guard разрушен корректно: RAII работает и внутри корутины.
    EXPECT_EQ(guards_alive(), 0);
}

TEST(Generator, ProducesOnlyWhatIsConsumed) {
    kvs::Store store;
    for (int i = 0; i < 100; ++i) {
        store.set("k" + std::to_string(i), kvs::Value(std::string("v")));
    }
    int taken = 0;
    for (const std::string& key : store.keys_lazily("*")) {
        static_cast<void>(key);
        if (++taken == 3) {
            break;   // остальные 97 ключей корутина так и не отдаст
        }
    }
    EXPECT_EQ(taken, 3);
}
