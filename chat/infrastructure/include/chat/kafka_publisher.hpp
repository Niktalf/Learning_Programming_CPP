#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "chat/events.hpp"

namespace chat {

// Этап 10: адаптер порта IEventPublisher к Kafka. Единственное место, где
// доменное событие встречается с брокером.
//
// В заголовке нет ни одного типа lab::kafka: конкретный класс спрятан в
// kafka_publisher.cpp, наружу видна только фабрика. Поэтому lab::kafka
// линкуется к chat_infrastructure как PRIVATE и не попадает в пути включения
// ни одной другой цели — даже точки сборки.
struct KafkaOptions {
    std::string brokers = "localhost:9092";
    std::string topic = "chat.events";
    // Граница очереди клиента. Переполнение — publish бросает, и сервер
    // теряет событие (docs/events.md, домашнее задание).
    std::size_t max_queued = 100000;
};

// Бросает ChatError, если обёртка отвергла параметры (например, пустой
// топик). К брокеру конструктор не подключается: сервер запускается и при
// лежащем брокере, события ждут в очереди клиента.
[[nodiscard]] std::unique_ptr<IEventPublisher> make_kafka_publisher(const KafkaOptions& options);

}  // namespace chat
