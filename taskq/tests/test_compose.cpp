// Этап 9: точка сборки с настоящими файлами. Интеграционные тесты — пишут во
// временный каталог и считают дескрипторы процесса, — поэтому живут в
// отдельной программе taskq_integration_tests, а модульные тесты к файловой
// системе не обращаются.

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "taskq/compose.hpp"
#include "taskq/errors.hpp"
#include "taskq/handler.hpp"
#include "temp_dir.hpp"

namespace {

taskq::Config with_log(const std::filesystem::path& path) {
    taskq::Config config;
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

TEST(Compose, JournalFileRecordsTransitions) {
    const taskq::testing::TempDir dir;
    const auto path = dir.file("transitions.log");
    {
        taskq::Service service = taskq::make_service(with_log(path));
        for (const char* const line : {"CREATE build", "SUBMIT build a", "POLL build", "ACK 1"}) {
            static_cast<void>(taskq::handle(service, line));
        }
    }
    EXPECT_EQ(lines_of(path),
              (std::vector<std::string>{"1\t-\tpending", "1\tpending\tleased", "1\tleased\tdone"}));
}

TEST(Compose, FailedConstructionLeaksNothing) {
    const taskq::testing::TempDir dir;
    const int before = taskq::testing::open_descriptors();
    EXPECT_THROW(static_cast<void>(taskq::make_service(with_log(dir.file("missing") / "transitions.log"))),
                 taskq::TaskQError);
    EXPECT_EQ(taskq::testing::open_descriptors(), before);
}

TEST(Compose, ConfigReachesTheService) {
    taskq::Config config;
    config.engine = taskq::EngineKind::Open;
    config.retry = taskq::RetryPolicy(2, std::chrono::seconds(5));
    taskq::Service service = taskq::make_service(config);
    EXPECT_EQ(service.store().engine_name(), "open");
    for (const char* const line : {"CREATE build", "SUBMIT build a", "POLL build"}) {
        static_cast<void>(taskq::handle(service, line));
    }
    EXPECT_EQ(taskq::handle(service, "FAIL 1").text, std::string("OK, retry 2 of 2 in 5s"));
}
