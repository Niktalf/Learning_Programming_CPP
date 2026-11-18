// examples/kafka_event_publisher_B_CHAT.cpp
//
// Заготовка адаптера для этапа 10, вариант B (CHAT).
// Копируется в infrastructure/ и дописывается. Это единственное место, где
// доменное событие встречается с брокером: ни domain, ни application про
// Kafka знать не должны.
//
// Файл компилируется как есть, но ничего полезного не делает: ключ и тело
// сообщения пусты. Места, которые заполняете вы, помечены словом ЗАДАНИЕ.

#include "lab/kafka/event_bus.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

// ---------------------------------------------------------------------------
// ЗАМЕНИТЕ этот блок подключением своего заголовка из domain/.
// Ниже лишь один из возможных видов портов, чтобы заготовка компилировалась.
// Ваши события могут выглядеть иначе — например, иерархией с виртуальной
// функцией вместо std::variant. Адаптер от этого не меняется по существу.
namespace domain {

struct MessagePosted {
    std::string room{};
    std::string author{};
    std::string text{};
};

struct UserJoined {
    std::string room{};
    std::string nick{};
};

struct UserLeft {
    std::string room{};
    std::string nick{};
};

using DomainEvent = std::variant<MessagePosted, UserJoined, UserLeft>;

class IEventPublisher {
public:
    virtual ~IEventPublisher() = default;
    virtual void publish(const DomainEvent& event) = 0;
};

}  // namespace domain
// ---------------------------------------------------------------------------

namespace infrastructure {

// Вспомогательный шаблон для std::visit с набором лямбд.
template <class... Fs>
struct overloaded : Fs... {
    using Fs::operator()...;
};
template <class... Fs>
overloaded(Fs...) -> overloaded<Fs...>;

class KafkaEventPublisher final : public domain::IEventPublisher {
public:
    // Топик вашего варианта — chat.events.
    explicit KafkaEventPublisher(lab::kafka::ProducerOptions options)
        : producer_(std::move(options)) {
        producer_.on_delivery([](const lab::kafka::DeliveryReport& report) {
            if (!report.ok) {
                // ЗАДАНИЕ 3. Брокер не принял сообщение. Что делать — ваше
                // решение, и оно должно быть осознанным: тихо проглотить
                // ошибку проще всего, но тогда события теряются молча.
                // Помните, что этот код выполняется в фоновом потоке
                // библиотеки. Выбранное поведение опишите в docs/events.md.
            }
        });
    }

    void publish(const domain::DomainEvent& event) override {
        // send не блокируется, даже если брокер лежит, но бросает
        // lab::kafka::Error при переполнении очереди. ЗАДАНИЕ 4: решите,
        // что с этим делать, — домашнее задание этапа 10.
        producer_.send(key_of(event), serialize(event));
    }

private:
    static std::string key_of(const domain::DomainEvent& event) {
        // ЗАДАНИЕ 1. Ключ сообщения.
        // Ключ определяет партицию, а порядок сообщений Kafka гарантирует
        // только внутри партиции. Пустой ключ, как сейчас, означает «любая
        // партиция» — порядок не гарантирован никакой.
        // Постановка этапа 10 называет ключ прямо. Ваша задача — объяснить
        // в комментарии, что этот выбор даёт: почему подписчик гарантированно
        // увидит вход участника раньше его первого сообщения в комнате и
        // почему для разных комнат такой гарантии нет и она не нужна.
        return std::visit(
            overloaded{
                [](const domain::MessagePosted& e) -> std::string {
                    static_cast<void>(e);
                    return {};   // ЗАДАНИЕ 1
                },
                [](const domain::UserJoined& e) -> std::string {
                    static_cast<void>(e);
                    return {};   // ЗАДАНИЕ 1
                },
                [](const domain::UserLeft& e) -> std::string {
                    static_cast<void>(e);
                    return {};   // ЗАДАНИЕ 1
                },
            },
            event);
    }

    static std::string serialize(const domain::DomainEvent& event) {
        // ЗАДАНИЕ 2. Тело сообщения.
        // Формат выбираете сами — строка с разделителями или JSON. Главное,
        // чтобы chat_tail мог его разобрать и чтобы по телу было понятно,
        // что это за событие.
        return std::visit(
            overloaded{
                [](const domain::MessagePosted& e) -> std::string {
                    // ЗАДАНИЕ 2: тело события MessagePosted
                    static_cast<void>(e);
                    return {};
                },
                [](const domain::UserJoined& e) -> std::string {
                    // ЗАДАНИЕ 2: тело события UserJoined
                    static_cast<void>(e);
                    return {};
                },
                [](const domain::UserLeft& e) -> std::string {
                    // ЗАДАНИЕ 2: тело события UserLeft
                    static_cast<void>(e);
                    return {};
                },
            },
            event);
    }

    lab::kafka::Producer producer_;
};

}  // namespace infrastructure

// ---------------------------------------------------------------------------
// Подключение в CMake:
//
//   find_package(lab_kafka REQUIRED)
//   target_link_libraries(<ваша цель infrastructure> PRIVATE lab::kafka)
//
// Цель domain с этой библиотекой не линкуется и линковаться не должна.
// Если понадобилось — слои разложены неверно.
// ---------------------------------------------------------------------------
