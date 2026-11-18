// examples/kafka_event_publisher_C_TASKQ.cpp
//
// Заготовка адаптера для этапа 10, вариант C (TASKQ).
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

struct TaskSubmitted {
    std::uint64_t id{};
    std::string queue{};
    int priority{};
};

struct TaskLeased {
    std::uint64_t id{};
    std::string worker{};
};

struct TaskCompleted {
    std::uint64_t id{};
};

struct TaskFailed {
    std::uint64_t id{};
    std::string reason{};
};

using DomainEvent = std::variant<TaskSubmitted, TaskLeased, TaskCompleted, TaskFailed>;

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
    // Топик вашего варианта — taskq.events.
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
        // в комментарии, что этот выбор даёт для жизненного цикла задачи,
        // и письменно разобрать альтернативу — ключ по имени очереди:
        // что она сохраняет и чем за это платит.
        return std::visit(
            overloaded{
                [](const domain::TaskSubmitted& e) -> std::string {
                    static_cast<void>(e);
                    return {};   // ЗАДАНИЕ 1
                },
                [](const domain::TaskLeased& e) -> std::string {
                    static_cast<void>(e);
                    return {};   // ЗАДАНИЕ 1
                },
                [](const domain::TaskCompleted& e) -> std::string {
                    static_cast<void>(e);
                    return {};   // ЗАДАНИЕ 1
                },
                [](const domain::TaskFailed& e) -> std::string {
                    static_cast<void>(e);
                    return {};   // ЗАДАНИЕ 1
                },
            },
            event);
    }

    static std::string serialize(const domain::DomainEvent& event) {
        // ЗАДАНИЕ 2. Тело сообщения.
        // Формат выбираете сами — строка с разделителями или JSON. Главное,
        // чтобы taskq_tail мог его разобрать и чтобы по телу было понятно,
        // что это за событие.
        return std::visit(
            overloaded{
                [](const domain::TaskSubmitted& e) -> std::string {
                    // ЗАДАНИЕ 2: тело события TaskSubmitted
                    static_cast<void>(e);
                    return {};
                },
                [](const domain::TaskLeased& e) -> std::string {
                    // ЗАДАНИЕ 2: тело события TaskLeased
                    static_cast<void>(e);
                    return {};
                },
                [](const domain::TaskCompleted& e) -> std::string {
                    // ЗАДАНИЕ 2: тело события TaskCompleted
                    static_cast<void>(e);
                    return {};
                },
                [](const domain::TaskFailed& e) -> std::string {
                    // ЗАДАНИЕ 2: тело события TaskFailed
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
