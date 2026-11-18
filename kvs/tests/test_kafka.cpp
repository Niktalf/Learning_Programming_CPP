// Этап 10: сквозной тест — сервер, адаптер, брокер, потребитель.
//
// Брокер — макет кластера из librdkafka внутри процесса: настоящий протокол
// Kafka поверх TCP, три партиции, но без Docker. Проверяется то, что
// модульные тесты проверить не могут: ключ сообщения, распределение по
// партициям и порядок внутри ключа, поведение при лежащем брокере.

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

#include "kvs/compose.hpp"
#include "kvs/errors.hpp"
#include "kvs/event_wire.hpp"
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

[[nodiscard]] kvs::Config config_for(const MockCluster& cluster, const std::string& topic) {
    kvs::Config config;
    config.events = kvs::KafkaOptions{.brokers = cluster.bootstrap(), .topic = topic, .max_queued = 100000};
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

}  // namespace

TEST(Kafka, EventsOfOneKeyKeepTheirOrder) {
    MockCluster cluster;
    cluster.topic("kvs.order");
    {
        kvs::Server server = kvs::make_server(config_for(cluster, "kvs.order"));
        for (const char* command : {"SET a 1", "SET b 1", "SET a 2", "RPUSH c x", "DEL a", "SET a 3", "GET a"}) {
            ASSERT_TRUE(server.handle(command).ok) << command;
        }
        // Разрушение сервера разрушает издателя, а тот дожидается отправки.
    }

    const std::vector<lab::kafka::Record> records = read(cluster, "kvs.order", 6);
    ASSERT_EQ(records.size(), 6u);   // GET события не даёт

    std::map<std::string, std::set<std::int32_t>> partitions;
    std::map<std::string, std::vector<std::string>> history;
    std::set<std::string> ids;
    for (const lab::kafka::Record& record : records) {
        const std::optional<kvs::EventEnvelope> envelope = kvs::decode_event(record.value);
        ASSERT_TRUE(envelope.has_value()) << record.value;
        EXPECT_EQ(record.key, kvs::key_of(envelope->event));   // ключ сообщения = ключ записи
        partitions[record.key].insert(record.partition);
        history[record.key].push_back(kvs::describe_event(envelope->event));
        ids.insert(envelope->id);
    }
    EXPECT_EQ(ids.size(), records.size());   // идентификаторы уникальны
    for (const auto& [key, used] : partitions) {
        EXPECT_EQ(used.size(), 1u) << "events of " << key << " spread over partitions";
    }
    // Партиции читаются вперемешку, но внутри ключа порядок — исходный.
    const std::vector<std::string> expected{"changed a (string, generation 1)", "changed a (string, generation 2)",
                                            "deleted a", "changed a (string, generation 1)"};
    EXPECT_EQ(history["a"], expected);
}

TEST(Kafka, CommandsWorkWhileBrokerIsDown) {
    MockCluster cluster;
    cluster.topic("kvs.down");
    cluster.down();
    kvs::Server server = kvs::make_server(config_for(cluster, "kvs.down"));
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(server.handle("INCR n").ok);
    }
    // send не ходит в сеть: сто команд при лежащем брокере — мгновенно.
    EXPECT_LT(std::chrono::steady_clock::now() - started, 1s);
    EXPECT_EQ(server.lost_events(), 0u);   // события ждут в очереди клиента
}

TEST(Kafka, FullQueueLosesEventsButKeepsData) {
    MockCluster cluster;
    cluster.topic("kvs.full");
    cluster.down();
    kvs::Config config = config_for(cluster, "kvs.full");
    config.events->max_queued = 3;
    kvs::Server server = kvs::make_server(config);
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(server.handle("INCR n").ok);   // клиенту не отказывают
    }
    EXPECT_EQ(server.lost_events(), 7u);
    EXPECT_EQ(server.handle("GET n").text, "10");
}

TEST(Kafka, EmptyTopicIsRejectedAtStart) {
    MockCluster cluster;
    kvs::Config config = config_for(cluster, "");
    EXPECT_THROW(static_cast<void>(kvs::make_server(config)), kvs::KvError);
}
