#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

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
    EXPECT_EQ(first->command().name(), std::string_view("GET"));
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

TEST(Cache, CachedCommandOutlivesNothingItBorrows) {
    // Связанная команда смотрит внутрь строки и токенов своего Request. Пока
    // запрос жив, исполнять её можно сколько угодно раз — в том числе MSET,
    // чей Rest смотрит в вектор токенов. Под санитайзером адресов висячее
    // представление здесь было бы видно сразу.
    RequestCache cache;
    const auto request = cache.get("MSET alpha 1 beta 2");
    kvs::Store store;
    static_cast<void>(request->command()(store));
    static_cast<void>(cache.get("GET padding-to-move-memory-around"));
    static_cast<void>(request->command()(store));
    EXPECT_EQ(store.size(), 2u);
}
