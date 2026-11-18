#include "kvs/kafka_publisher.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <print>
#include <random>
#include <string>
#include <utility>

#include "kvs/errors.hpp"
#include "kvs/event_wire.hpp"
#include "lab/kafka/event_bus.hpp"

namespace kvs {

namespace {

// Идентификатор экземпляра издателя: случайные 64 бита. Номера событий
// начинаются с нуля при каждом запуске, и без него id после перезапуска
// совпали бы с уже виденными — потребитель отбросил бы новые события как
// повторы.
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
        // отверг. Повторять нечем: тело уже у клиента, а не у нас. Событие
        // считается потерянным, и об этом — строка в stderr. Первая и затем
        // каждая тысячная, чтобы лежащий брокер не залил журнал.
        //
        // Код выполняется в фоновом потоке: трогает только atomic и stderr
        // (std::println в один FILE* потокобезопасен).
        producer_.on_delivery([stats = stats_](const lab::kafka::DeliveryReport& report) {
            if (report.ok) {
                return;
            }
            const std::uint64_t failed = ++stats->failed;
            if (failed == 1 || failed % 1000 == 0) {
                std::println(stderr, "[events] delivery failed for key '{}': {} ({} lost so far)", report.key,
                             report.error, failed);
            }
        });
    }

    // Ключ сообщения — ключ записи (key_of). Kafka хранит порядок только
    // внутри партиции, а партиция выбирается по ключу. Значит, все события
    // одного ключа KVS ложатся в одну партицию и приходят подписчику в том
    // порядке, в каком произошли: SET a, SET a, DEL a не превратится в DEL a,
    // SET a. Между разными ключами порядка нет — и он не нужен: события
    // разных записей друг от друга не зависят, а их распределение по трём
    // партициям даёт параллелизм потребителям одной группы.
    //
    // Случайный или пустой ключ разбросал бы события одного ключа по
    // партициям, и подписчик мог бы увидеть удаление раньше записи.
    void publish(const DomainEvent& event) override {
        const EventEnvelope envelope{std::format("{}:{}", instance_, next_++), event};
        // send не блокируется и не ходит в сеть. Бросает lab::kafka::Error
        // при переполнении очереди — исключение уходит к вызывающему
        // как есть: решение «терять или отказывать» принимает Server, а не
        // адаптер (docs/events.md).
        producer_.send(key_of(event), encode_event(envelope));
    }

private:
    std::shared_ptr<DeliveryStats> stats_ = std::make_shared<DeliveryStats>();
    lab::kafka::Producer producer_;   // после stats_: разрушается раньше, дожидаясь отправки
    const std::string instance_;
    std::uint64_t next_ = 0;
};

}  // namespace

std::unique_ptr<IEventPublisher> make_kafka_publisher(const KafkaOptions& options) {
    try {
        return std::make_unique<KafkaEventPublisher>(lab::kafka::ProducerOptions{
            .topic = options.topic,
            .brokers = options.brokers,
            .client_id = "kvs",
            // Остановка сервера ждёт неотправленные события не дольше
            // двух секунд: при лежащем брокере дольше ждать бессмысленно.
            .shutdown_flush = std::chrono::milliseconds(2000),
            .max_queued_messages = options.max_queued,
        });
    } catch (const lab::kafka::Error& e) {
        throw KvError(std::string("event publisher: ") + e.what());
    }
}

}  // namespace kvs
