// tools/lab_kafka_probe.cpp
//
// Проверка стенда одной командой: пишет сообщение в топик и читает его обратно.
// Используется при приёмке образа и студентами, когда «ничего не работает»,
// чтобы отделить сломанный стенд от сломанного кода.
//
//   lab_kafka_probe <топик> [брокеры]
//
//   lab_kafka_probe kvs.events      # вариант A
//   lab_kafka_probe chat.events     # вариант B
//   lab_kafka_probe taskq.events    # вариант C

#include "lab/kafka/event_bus.hpp"

#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <mutex>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "использование: lab_kafka_probe <топик> [брокеры]\n"
                     "  например:    lab_kafka_probe kvs.events\n";
        return 2;
    }
    const std::string topic = argv[1];
    const std::string brokers = (argc > 2) ? argv[2] : "localhost:9092";

    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    const std::string payload = std::format("probe-{}", stamp);

    std::cout << std::format("брокеры: {}\nтопик:   {}\n", brokers, topic) << std::flush;

    try {
        std::mutex m;
        std::string delivery_error;

        {
            lab::kafka::Producer producer({.topic = topic, .brokers = brokers});
            producer.on_delivery([&](const lab::kafka::DeliveryReport& r) {
                std::lock_guard lock(m);
                if (r.ok) {
                    std::cout << std::format("  доставлено: партиция {}, смещение {}\n",
                                             r.partition, r.offset);
                } else {
                    delivery_error = r.error;
                }
            });

            producer.send("probe", payload);
            if (!producer.flush(std::chrono::seconds(10))) {
                std::cout << "  ОШИБКА: брокер не подтвердил сообщение за 10 секунд\n"
                             "Проверьте, что стенд поднят: cd /opt/lab-env && docker compose up -d\n";
                return 1;
            }
        }
        {
            std::lock_guard lock(m);
            if (!delivery_error.empty()) {
                std::cout << std::format("  ОШИБКА доставки: {}\n", delivery_error);
                return 1;
            }
        }

        // Своя группа на каждый запуск, иначе повторный прогон не увидит
        // сообщение: смещение предыдущего запуска уже зафиксировано.
        lab::kafka::Consumer consumer({.topic = topic,
                                       .brokers = brokers,
                                       .group_id = std::format("probe-{}", stamp)});

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < deadline) {
            for (const auto& r : consumer.poll(std::chrono::milliseconds(500))) {
                if (r.value == payload) {
                    std::cout << std::format("  прочитано обратно: партиция {}, смещение {}\n",
                                             r.partition, r.offset)
                              << "СТЕНД РАБОТАЕТ\n";
                    return 0;
                }
            }
        }

        std::cout << "  ОШИБКА: сообщение записано, но не прочитано за 20 секунд\n";
        return 1;

    } catch (const lab::kafka::Error& e) {
        std::cout << std::format("  ОШИБКА: {}\n", e.what())
                  << "Проверьте, что стенд поднят: cd /opt/lab-env && docker compose up -d\n";
        return 1;
    }
}
