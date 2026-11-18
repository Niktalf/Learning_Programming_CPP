#include "chat/kafka_publisher.hpp"

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

#include "chat/errors.hpp"
#include "chat/event_wire.hpp"
#include "lab/kafka/event_bus.hpp"

namespace chat {

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
                std::println(stderr, "[events] delivery failed for room '{}': {} ({} lost so far)", report.key,
                             report.error, failed);
            }
        });
    }

    // Ключ сообщения — имя комнаты (room_of). Kafka хранит порядок только
    // внутри партиции, а партиция выбирается по ключу. Значит, все события
    // одной комнаты ложатся в одну партицию и приходят подписчику в том
    // порядке, в каком произошли: вход участника — раньше его первого
    // сообщения, сообщения — в порядке ленты. Между комнатами порядка нет, и
    // он не нужен: ленты разных комнат независимы.
    //
    // Ключ-ник был бы ошибкой: упорядоченными оказались бы события одного
    // пользователя, а лента комнаты, где пишут двое, могла бы перемешаться.
    void publish(const DomainEvent& event) override {
        const std::string& room = room_of(event);
        // Номер события в комнате расходуется до send, даже если send
        // бросит: тогда у потребителя будет разрыв в номерах, и он узнает о
        // событии, которое сервер потерял при переполненной очереди. Разрыв
        // — единственный способ сказать подписчику «здесь что-то было».
        const std::uint64_t number = ++numbers_[room];
        // send не блокируется и не ходит в сеть. Бросает lab::kafka::Error
        // при переполнении очереди — исключение уходит к вызывающему как
        // есть: решение «терять или отказывать» принимает Server.
        producer_.send(room, encode_event(EventEnvelope{instance_, number, event}));
    }

private:
    std::shared_ptr<DeliveryStats> stats_ = std::make_shared<DeliveryStats>();
    lab::kafka::Producer producer_;   // после stats_: разрушается раньше, дожидаясь отправки
    const std::string instance_;
    // Последний номер события по комнате. Комнаты в чате не удаляются, и
    // таблица растёт вместе с ними.
    std::unordered_map<std::string, std::uint64_t> numbers_;
};

}  // namespace

std::unique_ptr<IEventPublisher> make_kafka_publisher(const KafkaOptions& options) {
    try {
        return std::make_unique<KafkaEventPublisher>(lab::kafka::ProducerOptions{
            .topic = options.topic,
            .brokers = options.brokers,
            .client_id = "chat",
            // Остановка сервера ждёт неотправленные события не дольше
            // двух секунд: при лежащем брокере дольше ждать бессмысленно.
            .shutdown_flush = std::chrono::milliseconds(2000),
            .max_queued_messages = options.max_queued,
        });
    } catch (const lab::kafka::Error& e) {
        throw ChatError(std::string("event publisher: ") + e.what());
    }
}

}  // namespace chat
