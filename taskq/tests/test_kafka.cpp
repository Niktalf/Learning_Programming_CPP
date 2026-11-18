// Этап 10: сквозной тест — сервер, адаптер, брокер, потребитель.
//
// Брокер — макет кластера из librdkafka внутри процесса: настоящий протокол
// Kafka поверх TCP, три партиции, но без Docker. Проверяется то, что
// модульные тесты проверить не могут: ключ сообщения, распределение по
// партициям и порядок внутри задачи, поведение при лежащем брокере.

#include <gtest/gtest.h>

#include <librdkafka/rdkafka.h>
#include <librdkafka/rdkafka_mock.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "taskq/compose.hpp"
#include "taskq/handler.hpp"
#include "taskq/errors.hpp"
#include "taskq/event_wire.hpp"
#include "lab/kafka/event_bus.hpp"

namespace {

using namespace std::chrono_literals;

// Макет кластера на время теста.
class MockCluster {
public:
    MockCluster() {
        char error[256];
        rd_kafka_conf_t* const conf = rd_kafka_conf_new();
        rd_kafka_conf_set(conf, "log_level", "0", error, sizeof error);
        handle_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, error, sizeof error);
        cluster_ = rd_kafka_mock_cluster_new(handle_, 1);
        bootstrap_ = rd_kafka_mock_cluster_bootstraps(cluster_);
    }
    ~MockCluster() {
        rd_kafka_mock_cluster_destroy(cluster_);
        rd_kafka_destroy(handle_);
    }
    MockCluster(const MockCluster&) = delete;
    MockCluster& operator=(const MockCluster&) = delete;

    [[nodiscard]] const std::string& bootstrap() const noexcept { return bootstrap_; }
    void topic(const std::string& name) { rd_kafka_mock_topic_create(cluster_, name.c_str(), 3, 1); }
    void down() { rd_kafka_mock_broker_set_down(cluster_, 1); }

private:
    rd_kafka_t* handle_ = nullptr;
    rd_kafka_mock_cluster_t* cluster_ = nullptr;
    std::string bootstrap_;
};

[[nodiscard]] taskq::Config config_for(const MockCluster& cluster, const std::string& topic) {
    taskq::Config config;
    config.events = taskq::KafkaOptions{.brokers = cluster.bootstrap(), .topic = topic, .max_queued = 100000};
    return config;
}

// Прочитать из топика count сообщений или сдаться через 30 секунд.
[[nodiscard]] std::vector<lab::kafka::Record> read(const MockCluster& cluster, const std::string& topic,
                                                   std::size_t count) {
    lab::kafka::Consumer consumer({.topic = topic,
                                   .brokers = cluster.bootstrap(),
                                   .group_id = "test-" + topic,
                                   .from_beginning = true,
                                   .auto_commit = true,
                                   .join_timeout = 15000ms});
    std::vector<lab::kafka::Record> records;
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (records.size() < count && std::chrono::steady_clock::now() < deadline) {
        for (lab::kafka::Record& record : consumer.poll(500ms)) {
            records.push_back(std::move(record));
        }
    }
    return records;
}

[[nodiscard]] bool run(taskq::Service& service, const std::string& line) {
    return taskq::handle(service, line).ok;
}

}  // namespace

TEST(Kafka, LifecycleOfOneTaskKeepsItsOrder) {
    MockCluster cluster;
    cluster.topic("taskq.order");
    {
        taskq::Service service = taskq::make_service(config_for(cluster, "taskq.order"));
        for (const char* line : {"CREATE build", "WORKER w1", "SUBMIT build a", "SUBMIT build b", "SUBMIT build c",
                                 "POLL build w1", "POLL build w1", "RELEASE 1", "ACK 2", "POLL build w1",
                                 "FAIL 3", "POLL build", "ACK 1", "PURGE build"}) {
            ASSERT_TRUE(run(service, line)) << line;
        }
        // Разрушение сервиса разрушает издателя, а тот дожидается отправки.
    }
    // 1: submitted, leased, returned, leased, completed — 5;
    // 2: submitted, leased, completed — 3; 3: submitted, leased, failed — 3.
    const std::vector<lab::kafka::Record> records = read(cluster, "taskq.order", 11);
    ASSERT_EQ(records.size(), 11u);

    std::map<std::string, std::set<std::int32_t>> partitions;
    std::map<std::string, std::vector<std::string>> lifecycle;
    std::map<std::string, std::uint64_t> last;
    for (const lab::kafka::Record& record : records) {
        const std::optional<taskq::EventEnvelope> envelope = taskq::decode_event(record.value);
        ASSERT_TRUE(envelope.has_value()) << record.value;
        EXPECT_EQ(record.key, std::to_string(taskq::task_of(envelope->event)));   // ключ сообщения = задача
        partitions[record.key].insert(record.partition);
        lifecycle[record.key].push_back(taskq::describe_event(envelope->event));
        EXPECT_EQ(envelope->number, last[record.key] + 1);   // номера задачи подряд
        last[record.key] = envelope->number;
    }
    for (const auto& [task, used] : partitions) {
        EXPECT_EQ(used.size(), 1u) << "events of task " << task << " spread over partitions";
    }
    // Подтверждение не раньше постановки: жизненный цикл — в исходном порядке.
    const std::vector<std::string> expected{"task 1 submitted to build (priority 0)", "task 1 leased by w1",
                                            "task 1 returned (released)", "task 1 leased",
                                            "task 1 completed"};
    EXPECT_EQ(lifecycle["1"], expected);
}

TEST(Kafka, CommandsWorkWhileBrokerIsDown) {
    MockCluster cluster;
    cluster.topic("taskq.down");
    cluster.down();
    taskq::Service service = taskq::make_service(config_for(cluster, "taskq.down"));
    ASSERT_TRUE(run(service, "CREATE build"));
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(run(service, "SUBMIT build make"));
    }
    // send не ходит в сеть: сто постановок при лежащем брокере — мгновенно.
    EXPECT_LT(std::chrono::steady_clock::now() - started, 1s);
    EXPECT_EQ(service.lost_events(), 0u);   // события ждут в очереди клиента
}

TEST(Kafka, FullQueueLosesEventsButKeepsTasks) {
    MockCluster cluster;
    cluster.topic("taskq.full");
    cluster.down();
    taskq::Config config = config_for(cluster, "taskq.full");
    config.events->max_queued = 3;
    taskq::Service service = taskq::make_service(config);
    ASSERT_TRUE(run(service, "CREATE build"));
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(run(service, "SUBMIT build make"));   // клиенту не отказывают
    }
    EXPECT_EQ(service.lost_events(), 7u);
    EXPECT_EQ(taskq::handle(service, "STATUS 10").text, "pending in build");
}

TEST(Kafka, EmptyTopicIsRejectedAtStart) {
    MockCluster cluster;
    const taskq::Config config = config_for(cluster, "");
    EXPECT_THROW(static_cast<void>(taskq::make_service(config)), taskq::TaskQError);
}
