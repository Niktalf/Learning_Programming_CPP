// Этап 10: сквозной тест — сервер, адаптер, брокер, потребитель.
//
// Брокер — макет кластера из librdkafka внутри процесса: настоящий протокол
// Kafka поверх TCP, три партиции, но без Docker. Проверяется то, что
// модульные тесты проверить не могут: ключ сообщения, распределение по
// партициям и порядок внутри комнаты, поведение при лежащем брокере.

#include <gtest/gtest.h>

#include <librdkafka/rdkafka.h>
#include <librdkafka/rdkafka_mock.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "chat/compose.hpp"
#include "chat/errors.hpp"
#include "chat/event_wire.hpp"
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

[[nodiscard]] chat::Config config_for(const MockCluster& cluster, const std::string& topic) {
    chat::Config config;
    config.events = chat::KafkaOptions{.brokers = cluster.bootstrap(), .topic = topic, .max_queued = 100000};
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

TEST(Kafka, EventsOfOneRoomKeepTheirOrder) {
    MockCluster cluster;
    cluster.topic("chat.order");
    {
        chat::Server server = chat::make_server(config_for(cluster, "chat.order"));
        chat::Session alice = server.login("alice");
        chat::Session bob = server.login("bob");
        for (const auto& [who, line] : std::vector<std::pair<chat::Session*, std::string>>{
                 {&alice, "/join #a"}, {&bob, "/join #b"}, {&alice, "/msg #a one"}, {&bob, "/join #a"},
                 {&bob, "/msg #a two"}, {&bob, "/msg #a,#b three"}, {&alice, "/who #a"}}) {
            ASSERT_TRUE(server.handle(*who, line).ok) << line;
        }
        // Разрушение сервера разрушает издателя, а тот дожидается отправки.
    }
    // #a: joined, notice, one, joined, notice, two, three — 7;
    // #b: joined, notice, three — 3. /who событий не даёт.
    const std::vector<lab::kafka::Record> records = read(cluster, "chat.order", 10);
    ASSERT_EQ(records.size(), 10u);

    std::map<std::string, std::set<std::int32_t>> partitions;
    std::map<std::string, std::vector<std::string>> feed;
    std::map<std::string, std::uint64_t> last;
    for (const lab::kafka::Record& record : records) {
        const std::optional<chat::EventEnvelope> envelope = chat::decode_event(record.value);
        ASSERT_TRUE(envelope.has_value()) << record.value;
        EXPECT_EQ(record.key, chat::room_of(envelope->event));   // ключ сообщения = комната
        partitions[record.key].insert(record.partition);
        feed[record.key].push_back(chat::describe_event(envelope->event));
        EXPECT_EQ(envelope->number, last[record.key] + 1);        // номера комнаты подряд
        last[record.key] = envelope->number;
    }
    for (const auto& [room, used] : partitions) {
        EXPECT_EQ(used.size(), 1u) << "events of " << room << " spread over partitions";
    }
    // Вход bob в #a — раньше его сообщения: порядок комнаты сохранён.
    const std::vector<std::string>& a = feed["#a"];
    ASSERT_EQ(a.size(), 7u);
    EXPECT_EQ(a[3], "#a + bob joined");
    EXPECT_TRUE(a[5].starts_with("#a <bob> two"));
    EXPECT_TRUE(a[6].starts_with("#a <bob> three"));
}

TEST(Kafka, CommandsWorkWhileBrokerIsDown) {
    MockCluster cluster;
    cluster.topic("chat.down");
    cluster.down();
    chat::Server server = chat::make_server(config_for(cluster, "chat.down"));
    chat::Session alice = server.login("alice");
    ASSERT_TRUE(server.handle(alice, "/join #a").ok);
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(server.handle(alice, "/msg #a hello").ok);
    }
    // send не ходит в сеть: сто сообщений при лежащем брокере — мгновенно.
    EXPECT_LT(std::chrono::steady_clock::now() - started, 1s);
    EXPECT_EQ(server.lost_events(), 0u);   // события ждут в очереди клиента
}

TEST(Kafka, FullQueueLosesEventsButKeepsMessages) {
    MockCluster cluster;
    cluster.topic("chat.full");
    cluster.down();
    chat::Config config = config_for(cluster, "chat.full");
    config.events->max_queued = 3;
    chat::Server server = chat::make_server(config);
    chat::Session alice = server.login("alice");
    ASSERT_TRUE(server.handle(alice, "/join #a").ok);   // два события: вход и уведомление
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(server.handle(alice, "/msg #a hello").ok);   // отправителю не отказывают
    }
    EXPECT_EQ(server.lost_events(), 9u);
    EXPECT_EQ(server.store().history("#a")->get().size(), 11u);
}

TEST(Kafka, EmptyTopicIsRejectedAtStart) {
    MockCluster cluster;
    const chat::Config config = config_for(cluster, "");
    EXPECT_THROW(static_cast<void>(chat::make_server(config)), chat::ChatError);
}
