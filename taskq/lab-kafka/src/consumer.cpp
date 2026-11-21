// src/consumer.cpp
//
// Единственное место в обёртке, которое знает про API потребителя Kafka.

#include "lab/kafka/event_bus.hpp"
#include "detail.hpp"

#include <kafka/KafkaConsumer.h>

#include <cstdio>
#include <utility>

namespace lab::kafka {

namespace mcc = ::kafka::clients::consumer;

struct Consumer::Impl {
    ConsumerOptions options;
    std::unique_ptr<mcc::KafkaConsumer> consumer;
};

namespace {

// Шаблон, а не функция от Consumer::Impl: тип Impl закрыт, и назвать его здесь
// нельзя, а вывести из аргумента — можно.
template <class Impl>
Impl& alive(const std::unique_ptr<Impl>& impl) {
    if (!impl) {
        throw Error("потребитель перемещён и больше не пригоден к использованию");
    }
    return *impl;
}

}  // namespace

Consumer::Consumer(ConsumerOptions options) : impl_(std::make_unique<Impl>()) {
    if (options.topic.empty()) {
        throw Error("не указан топик: заполните ConsumerOptions::topic");
    }
    impl_->options = std::move(options);

    ::kafka::Properties props({
        {"bootstrap.servers",  {impl_->options.brokers}},
        {"group.id",           {impl_->options.group_id}},
        {"auto.offset.reset",  {impl_->options.from_beginning ? "earliest" : "latest"}},
        {"enable.auto.commit", {impl_->options.auto_commit ? "true" : "false"}},
        // Минимум, который разрешает брокер; по умолчанию 45 с. Потребитель,
        // убитый по Ctrl+C без закрытия, всё это время числится в группе,
        // и перезапущенный tail ждал бы его исчезновения почти минуту.
        {"session.timeout.ms",    {"6000"}},
        {"heartbeat.interval.ms", {"2000"}},
    });
    detail::quiet_logging(props);

    try {
        impl_->consumer = std::make_unique<mcc::KafkaConsumer>(props);
    } catch (const std::exception& e) {
        detail::rethrow("не удалось создать потребителя", e);
    }

    try {
        impl_->consumer->subscribe({impl_->options.topic},
                                   ::kafka::clients::consumer::NullRebalanceCallback,
                                   impl_->options.join_timeout);
    } catch (const std::exception& e) {
        throw Error("брокер " + impl_->options.brokers + " не назначил партиции топика " +
                    impl_->options.topic + " за " +
                    std::to_string(impl_->options.join_timeout.count()) +
                    " мс. Стенд поднят? (" + detail::describe(e) + ")");
    }
}

Consumer::~Consumer() {
    if (!impl_ || !impl_->consumer) {
        return;
    }
    try {
        impl_->consumer->close();
    } catch (...) {
    }
}

Consumer::Consumer(Consumer&&) noexcept = default;
Consumer& Consumer::operator=(Consumer&&) noexcept = default;

std::vector<Record> Consumer::poll(std::chrono::milliseconds timeout) {
    Impl& impl = alive(impl_);
    std::vector<Record> out;
    try {
        auto records = impl.consumer->poll(timeout);
        out.reserve(records.size());

        for (const auto& r : records) {
            if (r.error()) {
                // Ошибка отдельного сообщения не должна ронять весь опрос:
                // остальные записи в пачке корректны.
                std::fprintf(stderr, "[kafka] пропущена запись: %s\n",
                             r.error().message().c_str());
                continue;
            }
            Record record;
            // Байты как есть. ConstBuffer::toString из modern-cpp-kafka
            // печатает непечатаемые символы как «[0x09]» — для отладочного
            // вывода, а не для данных: табуляция в теле события превращалась
            // в шесть символов, и потребитель не мог разобрать сообщение.
            record.key = detail::bytes(r.key());
            record.value = detail::bytes(r.value());
            record.partition = r.partition();
            record.offset = r.offset();
            record.timestamp = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(r.timestamp().msSinceEpoch));
            out.push_back(std::move(record));
        }
    } catch (const std::exception& e) {
        detail::rethrow("poll", e);
    }
    return out;
}

void Consumer::commit() {
    Impl& impl = alive(impl_);
    if (impl.options.auto_commit) {
        return;   // фиксацией занимается сам клиент
    }
    try {
        impl.consumer->commitSync();
    } catch (const std::exception& e) {
        detail::rethrow("commit", e);
    }
}

}  // namespace lab::kafka
