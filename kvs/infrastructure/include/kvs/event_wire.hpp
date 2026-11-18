#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "kvs/events.hpp"

namespace kvs {

// Этап 10: формат события в теле сообщения Kafka. Общий для издателя
// (KafkaEventPublisher) и потребителя (kvs_tail), поэтому вынесен отдельно и
// проверяется модульными тестами без брокера.
//
// Одна строка, поля через табуляцию:
//
//   <id>  changed  <key>  <type>  <generation>
//   <id>  deleted  <key>
//   <id>  expired  <key>
//
// id — «экземпляр:номер»: случайный идентификатор процесса-издателя и номер
// события в нём. Он нужен потребителю, чтобы узнать повтор (звёздочка этапа
// 10): at-least-once означает, что одно и то же сообщение может прийти
// дважды. Табуляция, перевод строки и обратная косая черта в ключе
// экранируются: ключ KVS — произвольная строка.
//
// Текст, а не JSON: lab-stand events и kafka-console-consumer показывают
// тело как есть, и событие читается глазами без инструментов.
struct EventEnvelope {
    std::string id;
    DomainEvent event;
};

[[nodiscard]] std::string encode_event(const EventEnvelope& envelope);

// Пусто — тело не в этом формате: чужое сообщение в топике или испорченное.
[[nodiscard]] std::optional<EventEnvelope> decode_event(std::string_view payload);

// Одна строка для человека — то, что печатает kvs_tail.
[[nodiscard]] std::string describe_event(const DomainEvent& event);

}  // namespace kvs
