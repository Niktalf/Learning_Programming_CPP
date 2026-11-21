// Этап 9: точка сборки с настоящими файлами. Интеграционные тесты — пишут во
// временный каталог и считают дескрипторы процесса, — поэтому живут в
// отдельной программе chat_integration_tests, а модульные тесты к файловой
// системе не обращаются.

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "chat/compose.hpp"
#include "chat/errors.hpp"
#include "temp_dir.hpp"

namespace {

chat::Config with_log(const std::filesystem::path& path) {
    chat::Config config;
    config.log = path;
    return config;
}

std::vector<std::string> lines_of(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST(Compose, JournalFileRecordsSentMessages) {
    const chat::testing::TempDir dir;
    const auto path = dir.file("chat.log");
    {
        chat::Server server = chat::make_server(with_log(path));
        chat::Session alice = server.login("alice");
        static_cast<void>(server.handle(alice, "/join #a"));
        static_cast<void>(server.handle(alice, "/msg #a hello"));
        static_cast<void>(server.handle(alice, "/msg #absent lost"));
    }
    EXPECT_EQ(lines_of(path), (std::vector<std::string>{"#a\talice\thello"}));
}

TEST(Compose, FailedConstructionLeaksNothing) {
    const chat::testing::TempDir dir;
    const int before = chat::testing::open_descriptors();
    EXPECT_THROW(static_cast<void>(chat::make_server(with_log(dir.file("missing") / "chat.log"))), chat::ChatError);
    EXPECT_EQ(chat::testing::open_descriptors(), before);
}

TEST(Compose, EngineAndCapacityFromConfig) {
    chat::Config config;
    config.engine = chat::EngineKind::Open;
    config.history_capacity = 2;
    chat::Server server = chat::make_server(config);
    EXPECT_EQ(server.store().engine_name(), "open");
    EXPECT_EQ(server.store().history_capacity(), 2u);
}
