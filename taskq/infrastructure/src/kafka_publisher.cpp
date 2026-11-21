#include "taskq/kafka_publisher.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <print>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>

#include "taskq/errors.hpp"
#include "taskq/event_wire.hpp"
#include "lab/kafka/event_bus.hpp"

namespace taskq {

namespace {

// Идентификатор экземпляра издателя: случайные 64 бита. Номера событий
// начинаются заново при каждом запуске, и без него события нового запуска
// совпали бы с уже виденными — потребитель отбросил бы их как повторы.
[[nodiscard]] std::string make_instance_id() {
    std::random_device device;
    const std::uint64_t high = device();
    const std::uint64_t low = device();
    return std::format("{:016x}", (high << 32) ^ low);
}

// Счётчик сбоев доставки. Общий у издателя и обработчика доставки, который
// работает в фоновом потоке библиотеки, — поэтому atomic, а владение
// разделённое: обработчик не должен пережить счётчик.
struct DeliveryStats {
    std::atomic<std::uint64_t> failed{0};
};

class KafkaEventPublisher final : public IEventPublisher {
public:
    explicit KafkaEventPublisher(lab::kafka::ProducerOptions options)
        : producer_(std::move(options)), instance_(make_instance_id()) {
        // Брокер не принял сообщение — за пять минут не вернулся или
        // отверг. Событие потеряно; об этом — строка в stderr, первая и затем
        // каждая тысячная, чтобы лежащий брокер не залил журнал.
        //
        // Код выполняется в фоновом потоке: трогает только atomic и stderr.
        producer_.on_delivery([stats = stats_](const lab::kafka::DeliveryReport& report) {
            if (report.ok) {
                return;
            }
            const std::uint64_t failed = ++stats->failed;
            if (failed == 1 || failed % 1000 == 0) {
                std::println(stderr, "[events] delivery failed for task {}: {} ({} lost so far)", report.key,
                             report.error, failed);
            }
        });
    }

    // Ключ сообщения — номер задачи (task_of). Kafka хранит порядок только
    // внутри партиции, а партиция выбирается по ключу. Значит, все события
    // одной задачи ложатся в одну партицию и приходят подписчику в том
    // порядке, в каком произошли: постановка — раньше выдачи, выдача —
    // раньше подтверждения. Между задачами порядка нет, и он не нужен:
    // жизненные циклы разных задач независимы.
    //
    // Компромисс (docs/events.md): ключ-очередь тоже сохранил бы порядок по
    // задаче — задача не переходит между очередями, — но все события одной
    // очереди шли бы в одну партицию, и параллелизм потребителей упал бы до
    // числа очередей. Ключ-задача даёт его максимальным.
    void publish(const DomainEvent& event) override {
        const TaskId task = task_of(event);
        // Номер события задачи расходуется до send, даже если send бросит:
        // тогда у потребителя будет разрыв в номерах, и он узнает о
        // событии, которое сервис потерял при переполненной очереди.
        const auto counter = numbers_.try_emplace(task, 0).first;
        const std::uint64_t number = ++counter->second;
        // После последнего события задачи переходов не будет — счётчик
        // больше не нужен. Без этого таблица росла бы на каждую задачу.
        if (is_final(event)) {
            numbers_.erase(counter);
        }
        // send не блокируется и не ходит в сеть. Бросает lab::kafka::Error
        // при переполнении очереди — исключение уходит к вызывающему как
        // есть: решение «терять или отказывать» принимает Service.
        producer_.send(std::to_string(task), encode_event(EventEnvelope{instance_, number, event}));
    }

private:
    std::shared_ptr<DeliveryStats> stats_ = std::make_shared<DeliveryStats>();
    lab::kafka::Producer producer_;   // после stats_: разрушается раньше, дожидаясь отправки
    const std::string instance_;
    // Последний номер события по задаче — только для незавершённых задач.
    std::unordered_map<TaskId, std::uint64_t> numbers_;
};

}  // namespace

std::unique_ptr<IEventPublisher> make_kafka_publisher(const KafkaOptions& options) {
    try {
        return std::make_unique<KafkaEventPublisher>(lab::kafka::ProducerOptions{
            .topic = options.topic,
            .brokers = options.brokers,
            .client_id = "taskq",
            // Остановка сервера ждёт неотправленные события не дольше
            // двух секунд: при лежащем брокере дольше ждать бессмысленно.
            .shutdown_flush = std::chrono::milliseconds(2000),
            .max_queued_messages = options.max_queued,
        });
    } catch (const lab::kafka::Error& e) {
        throw TaskQError(std::string("event publisher: ") + e.what());
    }
}

}  // namespace taskq
