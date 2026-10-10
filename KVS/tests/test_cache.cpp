#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <variant>

#include "errors.hpp"
#include "request_cache.hpp"

TEST(Cache, ReturnsSameObjectWhileSomeoneOwnsIt) {
    RequestCache cache;
    const auto first = cache.get("SET a 1");
    const auto second = cache.get("SET a 1");

    EXPECT_EQ(first.get(), second.get());
}

TEST(Cache, DoesNotKeepRequestsAlive) {
    RequestCache cache;
    std::weak_ptr<const Request> observer;
    {
        const auto request = cache.get("SET a 1");
        observer = request;

        EXPECT_FALSE(observer.expired());
    }

    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(observer.lock(), nullptr);
}

TEST(Cache, PurgeRemovesExpiredEntries) {
    RequestCache cache;
    { const auto a = cache.get("SET a 1"); }
    const auto kept = cache.get("SET b 2");

    EXPECT_EQ(cache.size(), 2u);
    EXPECT_EQ(cache.purge(), 1u);
    EXPECT_EQ(cache.size(), 1u);
}

TEST(Cache, CommandStillBorrowsFromItsOwnLine) {
    RequestCache cache;
    const auto request = cache.get("SET key value");
    const auto& [key, value] = std::get<SetCommand>(request->command());

    EXPECT_EQ(key.data(), request->text().data() + 4);
}

TEST(Cache, ParseErrorPropagates) {
    RequestCache cache;

    EXPECT_THROW((void)cache.get("FLUSHALL"), ParseError);
    EXPECT_EQ(cache.size(), 0u);
}
