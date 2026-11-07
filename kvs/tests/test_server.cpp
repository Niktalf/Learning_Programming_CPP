#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "kvs/errors.hpp"
#include "kvs/file.hpp"
#include "kvs/log.hpp"
#include "kvs/server.hpp"
#include "temp_dir.hpp"

using kvs::Server;

namespace {

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

TEST(Server, SuccessPassesThrough) {
    Server server;
    const auto r = server.handle("SET k v");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.text, std::string("OK"));
}

TEST(Server, ParseErrorBecomesResponse) {
    Server server;
    const auto r = server.handle("FLUSHALL");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(starts_with(r.text, "ERR unknown command"));
}

TEST(Server, UnterminatedQuoteBecomesResponse) {
    Server server;
    const auto r = server.handle(R"(SET k "open)");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text, std::string("ERR unterminated quote"));
}

TEST(Server, MissingKeyBecomesResponse) {
    Server server;
    const auto r = server.handle("GET absent");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text, std::string("ERR no such key: absent (while executing GET absent)"));
}

TEST(Server, TypeMismatchBecomesResponse) {
    Server server;
    // handle помечена [[nodiscard]]: выброшенный ответ клиенту почти всегда
    // ошибка. Здесь вызов нужен только для подготовки, и это сказано явно.
    static_cast<void>(server.handle("SET s text"));
    const auto r = server.handle("INCR s");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text,
              std::string("ERR wrong type: expected int, got string (while executing INCR s)"));
}

TEST(Server, NeverThrowsOnGarbage) {
    Server server;
    // Граница не выпускает наружу ничего — это и есть её назначение.
    EXPECT_NO_THROW(static_cast<void>(server.handle("")));
    EXPECT_NO_THROW(static_cast<void>(server.handle("\"\"\"")));
    EXPECT_NO_THROW(static_cast<void>(server.handle("INCR k 99999999999999999999999")));
    EXPECT_NO_THROW(static_cast<void>(server.handle("MSET a")));
}

TEST(Server, FailedCommandDoesNotCorruptState) {
    Server server;
    static_cast<void>(server.handle("SET k v"));
    static_cast<void>(server.handle("MSET a 1 b"));   // нечётное число аргументов — отказ разбора
    EXPECT_EQ(server.store().size(), 1u);
    EXPECT_EQ(server.handle("GET k").text, std::string("v"));
}

TEST(Server, MemoryLimitBecomesResponse) {
    Server server(Server::Options{8, std::nullopt});
    EXPECT_TRUE(server.handle("SET k 1234").ok);
    const auto r = server.handle("MSET a 1 b 1234567");
    EXPECT_FALSE(r.ok);
    // Контекст с этапа 6 одинаков для всех команд — глагол и первый
    // аргумент. До реестра MSET называл первые два ключа.
    EXPECT_EQ(r.text, std::string("ERR out of memory: 15 bytes required, limit is 8 "
                                  "(while executing MSET a)"));
    EXPECT_EQ(server.store().size(), 1u);   // MSET не записал ни одной пары
}

TEST(Server, OverflowBecomesResponse) {
    Server server;
    static_cast<void>(server.handle("INCR n 9223372036854775807"));
    const auto r = server.handle("INCR n");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text, std::string("ERR increment would overflow (while executing INCR n)"));
}

// --- Этап 4: журнал и кэш на уровне сервера --------------------------------

namespace {

Server::Options with_log(const std::filesystem::path& path) {
    return Server::Options{std::nullopt, path};
}

}  // namespace

TEST(Server, JournalRestoresStateAfterRestart) {
    const kvs::testing::TempDir dir;
    const auto path = dir.file("journal.log");
    {
        Server server(with_log(path));
        static_cast<void>(server.handle("SET a 1"));
        static_cast<void>(server.handle("RPUSH list x"));
        static_cast<void>(server.handle("RPUSH list y"));
        static_cast<void>(server.handle("INCR counter 5"));
        static_cast<void>(server.handle("DEL a"));
    }
    Server restarted(with_log(path));
    EXPECT_EQ(restarted.handle("LLEN list").text, std::string("2"));
    EXPECT_EQ(restarted.handle("GET counter").text, std::string("5"));
    EXPECT_FALSE(restarted.handle("GET a").ok);
}

TEST(Server, OnlySuccessfulChangesAreJournaled) {
    const kvs::testing::TempDir dir;
    const auto path = dir.file("journal.log");
    {
        Server server(with_log(path));
        static_cast<void>(server.handle("SET a text"));
        static_cast<void>(server.handle("GET a"));      // чтение — не пишется
        static_cast<void>(server.handle("INCR a"));     // неудача — не пишется
        static_cast<void>(server.handle("FROB a"));     // не разобрано — не пишется
    }
    EXPECT_EQ(kvs::read_lines(path), (std::vector<std::string>{"SET a text"}));
}

TEST(Server, CorruptJournalStopsStartupWithContext) {
    const kvs::testing::TempDir dir;
    const auto path = dir.file("journal.log");
    {
        kvs::AppendOnlyLog log(path);
        log.append("SET a 1");
        log.append("FROB a");
    }
    try {
        const Server server(with_log(path));
        ADD_FAILURE() << "constructor must throw";
    } catch (const kvs::KvError& e) {
        EXPECT_NE(std::string(e.what()).find("line 2"), std::string::npos) << e.what();
    }
}

TEST(Server, FailedConstructionLeaksNothing) {
    // Конструктор бросил — деструктор Server не вызовется, но уже
    // построенные поля разрушатся. Дескрипторы это подтверждают.
    const kvs::testing::TempDir dir;
    const int before = kvs::testing::open_descriptors();
    EXPECT_THROW(Server(with_log(dir.file("missing") / "journal.log")), kvs::KvError);
    EXPECT_EQ(kvs::testing::open_descriptors(), before);
}

TEST(Server, ListCommands) {
    Server server;
    EXPECT_EQ(server.handle("RPUSH l a").text, std::string("1"));
    EXPECT_EQ(server.handle("RPUSH l b").text, std::string("2"));
    EXPECT_EQ(server.handle("LLEN l").text, std::string("2"));
    EXPECT_EQ(server.handle("TYPE l").text, std::string("list"));
    EXPECT_EQ(server.handle("GET l").text, std::string("[a, b]"));
    static_cast<void>(server.handle("SET s text"));
    EXPECT_EQ(server.handle("RPUSH s x").text,
              std::string("ERR wrong type: expected list, got string (while executing RPUSH s)"));
}

TEST(Server, CacheEntriesDoNotOutliveRequests) {
    Server server;
    for (int i = 0; i < 10; ++i) {
        static_cast<void>(server.handle("SET k" + std::to_string(i) + " v"));
    }
    // Каждый запрос отпущен после исполнения: в кэше одни истёкшие записи,
    // ни одного удерживаемого объекта.
    EXPECT_EQ(server.cache().hits(), 0u);
}
