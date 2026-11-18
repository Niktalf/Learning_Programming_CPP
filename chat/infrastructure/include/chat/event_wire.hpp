#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "chat/events.hpp"

namespace chat {

// Этап 10: формат события в теле сообщения Kafka. Общий для издателя
// (KafkaEventPublisher) и потребителя (chat_tail), поэтому вынесен отдельно и
// проверяется модульными тестами без брокера.
//
// Одна строка, поля через табуляцию:
//
//   <publisher>  <n>  posted  <room>  <author>  <kind>  <seq>  <text>
//   <publisher>  <n>  joined  <room>  <nick>
//   <publisher>  <n>  left    <room>  <nick>
//
// publisher — случайный идентификатор процесса-издателя, n — номер события
// в его комнате: у одной комнаты одного издателя номера идут подряд, 1, 2,
// 3… Пара «издатель, комната, n» однозначно называет событие — по ней
// потребитель узнаёт повтор (звёздочка этапа 10), а по разрыву в n — пропуск
// (домашнее задание). Табуляция, перевод строки и обратная косая черта в
// полях экранируются.
//
// Текст, а не JSON: lab-stand events показывает тело как есть.
struct EventEnvelope {
    std::string publisher;
    std::uint64_t number = 0;
    DomainEvent event;

    // Идентификатор события для поиска повторов: издатель, комната, номер.
    [[nodiscard]] std::string id() const;
};

[[nodiscard]] std::string encode_event(const EventEnvelope& envelope);

// Пусто — тело не в этом формате: чужое сообщение в топике или испорченное.
[[nodiscard]] std::optional<EventEnvelope> decode_event(std::string_view payload);

// Одна строка для человека — то, что печатает chat_tail.
[[nodiscard]] std::string describe_event(const DomainEvent& event);

}  // namespace chat
