#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace chat {

// Этап 10: доменные события чата и порт их публикации.
//
// Событие — факт предметной области словами чата: «в комнате появилось
// сообщение», «участник вошёл», «участник вышел». Здесь нет ни топика, ни
// партиции, ни типов клиента брокера. Как событие ляжет в брокер — ключ
// сообщения и тело, — решает адаптер в infrastructure/. Если в этот
// заголовок просочится тип клиента брокера, слой протёк, и цель
// chat_domain перестанет собираться изолированно (chat_domain_isolated).
struct MessagePosted {
    std::string room;
    std::string author;
    std::string text;
    std::string kind;        // text, notice, reaction
    std::uint64_t seq = 0;   // номер сообщения в чате (MessageId::seq)
};

struct UserJoined {
    std::string room;
    std::string nick;
};

struct UserLeft {
    std::string room;
    std::string nick;
};

using DomainEvent = std::variant<MessagePosted, UserJoined, UserLeft>;

// Комната, к которой относится событие, — у всех трёх она есть. По ней
// адаптер выбирает ключ сообщения.
[[nodiscard]] inline const std::string& room_of(const DomainEvent& event) noexcept {
    return std::visit([](const auto& e) -> const std::string& { return e.room; }, event);
}

// Порт публикации. Объявлен в domain — рядом с тем, кто порождает события;
// реализации — в infrastructure (брокер) и в тестах (FakePublisher).
//
// Контракт: publish не блокируется на сети — на этапе 11 он будет
// вызываться без мьютекса хранилища, и задерживать ответ клиенту он не
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

}  // namespace chat
