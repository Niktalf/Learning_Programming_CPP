#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <variant>

#include "kvs/errors.hpp"
#include "kvs/request_cache.hpp"

using kvs::RequestCache;

TEST(Cache, LiveRequestIsReused) {
    RequestCache cache;
    const auto first = cache.get("GET key");
    const auto second = cache.get("GET key");
    // Пока первый владелец жив, второй разбор не нужен: тот же объект.
    EXPECT_EQ(first.get(), second.get());
    EXPECT_EQ(cache.hits(), 1u);
    EXPECT_TRUE(std::holds_alternative<kvs::GetCommand>(first->command()));
}

TEST(Cache, CacheDoesNotKeepRequestsAlive) {
    RequestCache cache;
    std::weak_ptr<const kvs::Request> observer;
    {
        const auto request = cache.get("GET key");
        observer = request;
        EXPECT_FALSE(observer.expired());
    }
    // Владелец ушёл — объект уничтожен, хотя запись в кэше осталась.
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(observer.lock(), nullptr);
    EXPECT_EQ(cache.size(), 1u);

    // Повторный запрос — новый разбор, а не воскрешение старого объекта.
    const auto again = cache.get("GET key");
    EXPECT_EQ(cache.hits(), 0u);
    static_cast<void>(again);
}

TEST(Cache, PurgeRemovesExpiredEntries) {
    RequestCache cache;
    const auto kept = cache.get("GET kept");
    static_cast<void>(cache.get("GET gone1"));
    static_cast<void>(cache.get("GET gone2"));
    EXPECT_EQ(cache.size(), 3u);

    EXPECT_EQ(cache.purge(), 2u);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ(cache.get("GET kept").get(), kept.get());
}

TEST(Cache, GrowthIsBoundedWithoutExplicitPurge) {
    // Без очистки таблица росла бы на каждую новую строку — утечка, которую
    // санитайзер не покажет: объекты освобождены, ключи остались.
    RequestCache cache(16);
    for (int i = 0; i < 1000; ++i) {
        static_cast<void>(cache.get("GET key" + std::to_string(i)));
    }
    EXPECT_LE(cache.size(), 16u);
}

TEST(Cache, ParseErrorIsNotCached) {
    RequestCache cache;
    EXPECT_THROW(static_cast<void>(cache.get("FROB key")), kvs::ParseError);
    EXPECT_EQ(cache.size(), 0u);
}

TEST(Cache, CommandPointsIntoItsOwnLine) {
    RequestCache cache;
    const auto request = cache.get("GET key");
    const auto& get = std::get<kvs::GetCommand>(request->command());
    // Команда и строка живут в одном объекте: представление указывает внутрь.
    EXPECT_EQ(get.key.data(), request->text().data() + 4);
}
