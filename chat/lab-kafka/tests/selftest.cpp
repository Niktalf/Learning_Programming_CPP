// tests/selftest.cpp
//
// Самопроверка обёртки без стенда. librdkafka содержит макет кластера Kafka:
// он поднимается внутри процесса, слушает TCP-порт и говорит по настоящему
// протоколу. Обёртка проходит тот же путь, что и со стендом, только без Docker.
//
//   lab_kafka_selftest            # все проверки
//   lab_kafka_selftest commit     # одна проверка по имени

#include "lab/kafka/event_bus.hpp"

#include <librdkafka/rdkafka.h>
#include <librdkafka/rdkafka_mock.h>

#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("    НЕ ВЫПОЛНЕНО %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

long long ms_since(Clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
}

class MockCluster {
public:
    MockCluster() {
        char err[256];
        rd_kafka_conf_t* conf = rd_kafka_conf_new();
        rd_kafka_conf_set(conf, "log_level", "0", err, sizeof err);
        rk_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, err, sizeof err);
        mc_ = rd_kafka_mock_cluster_new(rk_, 1);
        bootstrap_ = rd_kafka_mock_cluster_bootstraps(mc_);
    }
    ~MockCluster() {
        rd_kafka_mock_cluster_destroy(mc_);
        rd_kafka_destroy(rk_);
    }
    MockCluster(const MockCluster&) = delete;
    MockCluster& operator=(const MockCluster&) = delete;

    const std::string& bootstrap() const { return bootstrap_; }
    void topic(const std::string& name) { rd_kafka_mock_topic_create(mc_, name.c_str(), 3, 1); }
    void down() { rd_kafka_mock_broker_set_down(mc_, 1); }
    void up() { rd_kafka_mock_broker_set_up(mc_, 1); }

private:
    rd_kafka_t* rk_ = nullptr;
    rd_kafka_mock_cluster_t* mc_ = nullptr;
    std::string bootstrap_;
};

std::vector<lab::kafka::Record> read_n(lab::kafka::Consumer& c, std::size_t n,
                                       std::chrono::milliseconds limit = 20s) {
    std::vector<lab::kafka::Record> all;
    const auto deadline = Clock::now() + limit;
    while (all.size() < n && Clock::now() < deadline) {
        for (auto& r : c.poll(300ms)) {
            all.push_back(std::move(r));
        }
    }
    return all;
}

// ------------------------------------------------------------------ проверки

void roundtrip(MockCluster& mc) {
    for (const std::string topic : {"kvs.events", "chat.events", "taskq.events"}) {
        std::printf("  топик %s\n", topic.c_str());
        mc.topic(topic);
        std::mutex m;
        std::vector<lab::kafka::DeliveryReport> reports;

        lab::kafka::Producer p({.topic = topic, .brokers = mc.bootstrap()});
        p.on_delivery([&](const lab::kafka::DeliveryReport& r) {
            std::lock_guard lock(m);
            reports.push_back(r);
        });

        const std::vector<std::string> keys{"a", "b", "c", "d", "e"};
        for (int i = 0; i < 40; ++i) {
            std::string key = keys[i % keys.size()];   // временная строка: ключ обязан копироваться
            p.send(key, "v" + std::to_string(i));
        }
        CHECK(p.flush(10s));
        CHECK(p.pending() == 0);

        {
            std::lock_guard lock(m);
            CHECK(reports.size() == 40);
            std::map<std::string, std::int32_t> partition_of;
            for (const auto& r : reports) {
                CHECK(r.ok);
                CHECK(r.offset >= 0);
                auto [it, inserted] = partition_of.emplace(r.key, r.partition);
                CHECK(it->second == r.partition);   // один ключ — одна партиция
            }
            CHECK(partition_of.size() == keys.size());
        }

        lab::kafka::Consumer c({.topic = topic, .brokers = mc.bootstrap(), .group_id = "g-" + topic});
        auto records = read_n(c, 40);
        CHECK(records.size() == 40);
        std::map<std::string, int> last;
        for (const auto& r : records) {
            const int n = std::stoi(r.value.substr(1));
            if (auto it = last.find(r.key); it != last.end()) {
                CHECK(n > it->second);              // порядок внутри ключа сохранён
            }
            last[r.key] = n;
            CHECK(r.timestamp.time_since_epoch().count() > 0);
        }
    }
}

// Тело и ключ доходят байт в байт: табуляция, перевод строки, нулевой байт
// и не-ASCII не должны превращаться в отладочное «[0x09]».
void binary_payload(MockCluster& mc) {
    const std::string topic = "bytes.events";
    mc.topic(topic);
    const std::string key("k\tя", 4);
    const std::string payload("id\tchanged\ta\nb\0c", 16);
    {
        lab::kafka::Producer p({.topic = topic, .brokers = mc.bootstrap()});
        p.send(key, payload);
        CHECK(p.flush(10s));
    }
    lab::kafka::Consumer c({.topic = topic, .brokers = mc.bootstrap(), .group_id = "g-bytes"});
    const auto records = read_n(c, 1);
    CHECK(records.size() == 1);
    if (!records.empty()) {
        CHECK(records[0].key == key);
        CHECK(records[0].value == payload);
    }
}

void required_topic(MockCluster& mc) {
    bool producer_threw = false, consumer_threw = false;
    try {
        lab::kafka::Producer p({.topic = "", .brokers = mc.bootstrap()});
    } catch (const lab::kafka::Error& e) {
        producer_threw = true;
        std::printf("    %s\n", e.what());
    }
    try {
        lab::kafka::Consumer c({.topic = "", .brokers = mc.bootstrap()});
    } catch (const lab::kafka::Error&) {
        consumer_threw = true;
    }
    CHECK(producer_threw);
    CHECK(consumer_threw);
}

void move(MockCluster& mc) {
    mc.topic("move.events");
    std::atomic<int> ok{0};
    lab::kafka::Producer a({.topic = "move.events", .brokers = mc.bootstrap()});
    a.on_delivery([&](const lab::kafka::DeliveryReport& r) { if (r.ok) ++ok; });
    for (int i = 0; i < 10; ++i) a.send("k", "x");
    lab::kafka::Producer b = std::move(a);
    CHECK(b.flush(10s));
    CHECK(ok == 10);

    CHECK(a.pending() == 0);                       // NOLINT: проверка перемещённого
    bool threw = false;
    try {
        a.send("k", "x");                          // NOLINT
    } catch (const lab::kafka::Error& e) {
        threw = true;
        std::printf("    %s\n", e.what());
    }
    CHECK(threw);
}

void destructor_flushes(MockCluster& mc) {
    mc.topic("dtor.events");
    std::atomic<int> ok{0};
    {
        lab::kafka::Producer p({.topic = "dtor.events", .brokers = mc.bootstrap()});
        p.on_delivery([&](const lab::kafka::DeliveryReport& r) { if (r.ok) ++ok; });
        for (int i = 0; i < 100; ++i) p.send("k" + std::to_string(i), "x");
    }
    CHECK(ok == 100);
}

void destructor_broker_down(MockCluster& mc) {
    mc.topic("dd.events");
    std::atomic<int> failed{0};
    std::string first_error;
    std::mutex m;
    Clock::time_point t0;
    {
        lab::kafka::Producer p({.topic = "dd.events", .brokers = mc.bootstrap(),
                                .shutdown_flush = 1000ms});
        p.on_delivery([&](const lab::kafka::DeliveryReport& r) {
            if (!r.ok) {
                std::lock_guard lock(m);
                if (first_error.empty()) first_error = r.error;
                ++failed;
            }
        });
        p.send("k", "warm");
        CHECK(p.flush(10s));
        mc.down();
        for (int i = 0; i < 5; ++i) p.send("k", "x");
        t0 = Clock::now();
    }
    const auto spent = ms_since(t0);
    std::printf("    деструктор: %lld мс, отчёт: %s\n", spent, first_error.c_str());
    CHECK(spent < 3000);
    CHECK(failed == 5);
    mc.up();
}

void broker_down_send(MockCluster& mc) {
    mc.topic("down.events");
    std::atomic<int> ok{0};
    lab::kafka::Producer p({.topic = "down.events", .brokers = mc.bootstrap()});
    p.on_delivery([&](const lab::kafka::DeliveryReport& r) { if (r.ok) ++ok; });
    p.send("k", "warm");
    CHECK(p.flush(10s));

    mc.down();
    const auto t0 = Clock::now();
    for (int i = 0; i < 100; ++i) p.send("k", "x");
    const auto spent = ms_since(t0);
    std::printf("    100 send при лежащем брокере: %lld мс\n", spent);
    CHECK(spent < 1000);
    CHECK(p.pending() == 100);
    CHECK(!p.flush(1s));

    mc.up();
    CHECK(p.flush(30s));
    CHECK(ok == 101);   // ничего не потеряно, порядок восстановлен клиентом
}

void queue_overflow(MockCluster& mc) {
    mc.topic("overflow.events");
    lab::kafka::Producer p({.topic = "overflow.events", .brokers = mc.bootstrap(),
                            .max_queued_messages = 10});
    mc.down();   // очередь гарантированно не опустеет
    bool threw = false;
    const auto t0 = Clock::now();
    try {
        for (int i = 0; i < 1000; ++i) p.send("k", "x");
    } catch (const lab::kafka::Error& e) {
        threw = true;
        std::printf("    %s (через %lld мс)\n", e.what(), ms_since(t0));
    }
    CHECK(threw);
    CHECK(ms_since(t0) < 1000);   // отказ немедленный, без ожидания
    mc.up();
}

void manual_commit(MockCluster& mc) {
    mc.topic("commit.events");
    {
        lab::kafka::Producer p({.topic = "commit.events", .brokers = mc.bootstrap()});
        for (int i = 0; i < 5; ++i) p.send("k", std::to_string(i));
        CHECK(p.flush(10s));
    }
    const lab::kafka::ConsumerOptions opts{.topic = "commit.events", .brokers = mc.bootstrap(),
                                           .group_id = "gc", .auto_commit = false};
    {
        lab::kafka::Consumer c(opts);
        CHECK(read_n(c, 5).size() == 5);           // без commit
    }
    {
        lab::kafka::Consumer c(opts);
        CHECK(read_n(c, 5).size() == 5);           // повторная доставка
        c.commit();
    }
    {
        lab::kafka::Consumer c(opts);
        CHECK(read_n(c, 1, 5s).empty());           // после commit повторов нет
    }
}

void restart_after_kill(MockCluster& mc) {
    // Потребитель, убитый без закрытия (Ctrl+C в kvs_tail), остаётся в группе
    // до истечения сессии. Перезапуск должен уложиться в join_timeout.
    mc.topic("kill.events");
    const lab::kafka::ConsumerOptions opts{.topic = "kill.events", .brokers = mc.bootstrap(),
                                           .group_id = "gk"};
    // Отдельный процесс, а не fork: после fork в потомке нет фоновых потоков
    // librdkafka, и создавать в нём новых клиентов небезопасно.
    std::fflush(stdout);
    const pid_t child = fork();
    if (child == 0) {
        execl("/proc/self/exe", "lab_kafka_selftest", "--join-and-die",
              mc.bootstrap().c_str(), static_cast<char*>(nullptr));
        _exit(4);
    }
    int status = 0;
    waitpid(child, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    const auto t0 = Clock::now();
    try {
        lab::kafka::Consumer c(opts);
        std::printf("    перезапуск вошёл в группу за %lld мс\n", ms_since(t0));
    } catch (const lab::kafka::Error& e) {
        std::printf("    %s\n", e.what());
        CHECK(false);
    }
}

void consumer_broker_down(MockCluster& mc) {
    mc.down();
    const auto t0 = Clock::now();
    bool threw = false;
    try {
        lab::kafka::Consumer c({.topic = "kvs.events", .brokers = mc.bootstrap(),
                                .group_id = "gd", .join_timeout = 3000ms});
    } catch (const lab::kafka::Error& e) {
        threw = true;
        std::printf("    %s\n", e.what());
    }
    const auto spent = ms_since(t0);
    CHECK(threw);
    CHECK(spent >= 3000 && spent < 6000);
    mc.up();
}

void wrong_address() {
    const auto t0 = Clock::now();
    {
        lab::kafka::Producer p({.topic = "x", .brokers = "127.0.0.1:1",
                                .shutdown_flush = 500ms});
        p.send("k", "v");
        CHECK(!p.flush(500ms));
    }
    CHECK(ms_since(t0) < 3000);   // деструктор не висит на недоступном адресе
}

}  // namespace

// Вспомогательный режим для restart_after_kill: войти в группу и умереть,
// не закрывая потребителя.
int join_and_die(const char* bootstrap) {
    try {
        lab::kafka::Consumer c({.topic = "kill.events", .brokers = bootstrap, .group_id = "gk"});
        c.poll(200ms);
    } catch (...) {
        _exit(3);
    }
    _exit(0);
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--join-and-die") {
        return join_and_die(argv[2]);
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const std::string only = argc > 1 ? argv[1] : "";

    MockCluster mc;
    std::printf("макет кластера: %s\n", mc.bootstrap().c_str());

    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"roundtrip", [&] { roundtrip(mc); }},
        {"binary_payload", [&] { binary_payload(mc); }},
        {"required_topic", [&] { required_topic(mc); }},
        {"move", [&] { move(mc); }},
        {"destructor_flushes", [&] { destructor_flushes(mc); }},
        {"destructor_broker_down", [&] { destructor_broker_down(mc); }},
        {"broker_down_send", [&] { broker_down_send(mc); }},
        {"queue_overflow", [&] { queue_overflow(mc); }},
        {"commit", [&] { manual_commit(mc); }},
        {"restart_after_kill", [&] { restart_after_kill(mc); }},
        {"consumer_broker_down", [&] { consumer_broker_down(mc); }},
        {"wrong_address", [] { wrong_address(); }},
    };

    for (const auto& [name, fn] : tests) {
        if (!only.empty() && only != name) continue;
        const int before = failures;
        const auto t0 = Clock::now();
        std::printf("%s\n", name);
        fn();
        std::printf("  %s, %lld мс\n", failures == before ? "ок" : "ПРОВАЛ", ms_since(t0));
    }

    std::printf(failures ? "ПРОВАЛЕНО ПРОВЕРОК: %d\n" : "ВСЁ ПРОШЛО\n", failures);
    return failures ? 1 : 0;
}
