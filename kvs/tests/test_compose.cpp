// Этап 9: точка сборки с настоящими файлами. Это интеграционные тесты — они
// пишут во временный каталог и считают дескрипторы процесса, — поэтому
// живут в отдельной программе kvs_integration_tests, а модульные тесты
// (kvs_tests) к файловой системе не обращаются.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "kvs/compose.hpp"
#include "kvs/errors.hpp"
#include "kvs/file.hpp"
#include "kvs/log.hpp"
#include "temp_dir.hpp"

namespace {

kvs::Config with_log(const std::filesystem::path& path) {
    kvs::Config config;
    config.log = path;
    return config;
}

}  // namespace

TEST(Compose, JournalFileRestoresStateAfterRestart) {
    const kvs::testing::TempDir dir;
    const auto path = dir.file("journal.log");
    {
        kvs::Server server = kvs::make_server(with_log(path));
        static_cast<void>(server.handle("SET a 1"));
        static_cast<void>(server.handle("RPUSH list x"));
        static_cast<void>(server.handle("INCR counter 5"));
        static_cast<void>(server.handle("GET a"));   // чтение — не пишется
    }
    EXPECT_EQ(kvs::read_lines(path), (std::vector<std::string>{"SET a 1", "RPUSH list x", "INCR counter 5"}));
    kvs::Server restarted = kvs::make_server(with_log(path));
    EXPECT_EQ(restarted.handle("LLEN list").text, std::string("1"));
    EXPECT_EQ(restarted.handle("GET counter").text, std::string("5"));
}

TEST(Compose, CorruptJournalFileNamesPathAndLine) {
    const kvs::testing::TempDir dir;
    const auto path = dir.file("journal.log");
    {
        kvs::AppendOnlyLog log(path);
        log.append("SET a 1");
        log.append("FROB a");
    }
    try {
        static_cast<void>(kvs::make_server(with_log(path)));
        ADD_FAILURE() << "make_server must throw";
    } catch (const kvs::KvError& e) {
        EXPECT_NE(std::string(e.what()).find(path.string() + " line 2"), std::string::npos) << e.what();
    }
}

TEST(Compose, FailedConstructionLeaksNothing) {
    // Журнал не открылся — сервер не построен, и ни один дескриптор не
    // остался открытым.
    const kvs::testing::TempDir dir;
    const int before = kvs::testing::open_descriptors();
    EXPECT_THROW(static_cast<void>(kvs::make_server(with_log(dir.file("missing") / "journal.log"))), kvs::KvError);
    EXPECT_EQ(kvs::testing::open_descriptors(), before);
}

TEST(Compose, EngineAndLimitFromConfig) {
    kvs::Config config;
    config.engine = kvs::EngineKind::Open;
    config.memory_limit = 8;
    kvs::Server server = kvs::make_server(config);
    EXPECT_EQ(server.store().engine_name(), "open");
    EXPECT_FALSE(server.handle("SET key 123456789").ok);
}
