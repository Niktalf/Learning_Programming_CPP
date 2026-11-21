#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace kvs {

// Этап 10: доменные события и порт их публикации.
//
// Событие — факт предметной области: «ключ изменился», «ключ удалён»,
// «ключ истёк». Оно описано словами KVS, а не брокера: здесь нет ни топика,
// ни партиции, ни ProducerRecord. Как событие ляжет в брокер — какой у него
// будет ключ сообщения и тело, — решает адаптер в infrastructure/. Если в
// этот заголовок когда-нибудь просочится тип клиента брокера, слой протёк,
// и цель kvs_domain перестанет собираться изолированно (CMakeLists.txt,
// kvs_domain_isolated).
//
// generation — поколение записи (RecordVersion::generation): сколько раз
// ключ записывали. По нему подписчик видит, что пропустил событие: у
// одного ключа поколения идут подряд.
struct KeyChanged {
    std::string key;
    std::string type;   // тип значения: string, int, list
    std::uint64_t generation = 0;
};

struct KeyDeleted {
    std::string key;
};

// Истечение срока — этап 11 (TTL). Тип объявлен заранее: подписчики и
// адаптер уже умеют его передавать, а хранилище начнёт его порождать, когда
// у ключей появится срок.
struct KeyExpired {
    std::string key;
};

using DomainEvent = std::variant<KeyChanged, KeyDeleted, KeyExpired>;

// Имя ключа, к которому относится событие, — у всех трёх оно есть.
[[nodiscard]] inline const std::string& key_of(const DomainEvent& event) noexcept {
    return std::visit([](const auto& e) -> const std::string& { return e.key; }, event);
}

// Порт публикации. Объявлен в domain — рядом с тем, кто порождает события;
// реализации — в infrastructure (брокер) и в тестах (FakePublisher).
//
// Контракт: publish не должен блокироваться на сети — на этапе 11 он будет
// вызываться без мьютекса хранилища, но и задерживать ответ клиенту он не
// вправе. Бросить publish может; что тогда делать, решает вызывающий
// (Server: событие теряется, команда остаётся выполненной — docs/events.md).
class IEventPublisher {
public:
    virtual ~IEventPublisher() = default;
    virtual void publish(const DomainEvent& event) = 0;

protected:
    IEventPublisher() = default;
    IEventPublisher(const IEventPublisher&) = default;
    IEventPublisher& operator=(const IEventPublisher&) = default;
};

}  // namespace kvs
