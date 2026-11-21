#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "taskq/events.hpp"

namespace taskq {

// Этап 10: формат события в теле сообщения Kafka. Общий для издателя
// (KafkaEventPublisher) и потребителя (taskq_tail), поэтому вынесен отдельно
// и проверяется модульными тестами без брокера.
//
// Одна строка, поля через табуляцию:
//
//   <publisher>  <n>  submitted  <task>  <queue>  <priority>
//   <publisher>  <n>  leased     <task>  <worker>
//   <publisher>  <n>  completed  <task>
//   <publisher>  <n>  failed     <task>  <reason>
//   <publisher>  <n>  returned   <task>  <reason>
//   <publisher>  <n>  cancelled  <task>
//
// publisher — случайный идентификатор процесса-сервиса, n — номер события
// задачи у этого издателя: 1, 2, 3… Тройка «издатель, задача, n»
// однозначно называет событие — по ней потребитель узнаёт повтор
// (звёздочка этапа 10), а по разрыву в n — пропуск (домашнее задание).
// Табуляция, перевод строки и обратная косая черта в полях экранируются.
//
// Текст, а не JSON: lab-stand events показывает тело как есть.
struct EventEnvelope {
    std::string publisher;
    std::uint64_t number = 0;
    DomainEvent event;

    // Идентификатор события для поиска повторов: издатель, задача, номер.
    [[nodiscard]] std::string id() const;
};

[[nodiscard]] std::string encode_event(const EventEnvelope& envelope);

// Пусто — тело не в этом формате: чужое сообщение в топике или испорченное.
[[nodiscard]] std::optional<EventEnvelope> decode_event(std::string_view payload);

// Одна строка для человека — то, что печатает taskq_tail.
[[nodiscard]] std::string describe_event(const DomainEvent& event);

}  // namespace taskq
