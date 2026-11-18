// src/producer.cpp
//
// Единственное место в обёртке, которое знает про API продюсера Kafka.
// Если библиотека сменится, переписывать нужно только этот файл и consumer.cpp,
// а публичный заголовок и весь код студентов останутся прежними.
//
// Собирается с modern-cpp-kafka v2024.07.03 — версия закреплена в инструкции
// по установке ПО.

#include "lab/kafka/event_bus.hpp"
#include "detail.hpp"

#include <kafka/KafkaProducer.h>

#include <atomic>
#include <mutex>
#include <utility>

namespace lab::kafka {

namespace mck = ::kafka::clients::producer;

struct Producer::Impl {
    ProducerOptions options;
    std::unique_ptr<mck::KafkaProducer> producer;

    std::mutex handler_mutex;
    std::function<void(const DeliveryReport&)> handler;

    std::atomic<std::size_t> in_flight{0};
};

namespace {

// Шаблон, а не функция от Producer::Impl: тип Impl закрыт, и назвать его здесь
// нельзя, а вывести из аргумента — можно.
template <class Impl>
Impl& alive(const std::unique_ptr<Impl>& impl) {
    if (!impl) {
        throw Error("продюсер перемещён и больше не пригоден к использованию");
    }
    return *impl;
}

}  // namespace

Producer::Producer(ProducerOptions options) : impl_(std::make_unique<Impl>()) {
    if (options.topic.empty()) {
        throw Error("не указан топик: заполните ProducerOptions::topic");
    }
    impl_->options = std::move(options);

    ::kafka::Properties props({
        {"bootstrap.servers", {impl_->options.brokers}},
        {"client.id",         {impl_->options.client_id}},
        {"queue.buffering.max.messages",
                              {std::to_string(impl_->options.max_queued_messages)}},
        // Ждём подтверждения от всех синхронизированных реплик. На одном
        // брокере это ничего не меняет, но делает намерение явным: мы не
        // хотим терять события ради скорости.
        {"acks",              {"all"}},
        // Клиент сам повторяет отправку при временных сбоях; порядок
        // сообщений при этом сохраняется, а дубликатов не появляется.
        {"enable.idempotence", {"true"}},
    });
    detail::quiet_logging(props);

    try {
        impl_->producer = std::make_unique<mck::KafkaProducer>(props);
    } catch (const std::exception& e) {
        detail::rethrow("не удалось создать продюсер", e);
    }
}

Producer::~Producer() {
    if (!impl_ || !impl_->producer) {
        return;
    }
    // Деструктор обязан быть noexcept, поэтому гасим всё, что может прилететь
    // из библиотеки. Потерять события на выходе неприятно, но вызвать
    // std::terminate во время раскрутки стека — хуже.
    //
    // close с таймаутом дожидается отправки, а не уложившееся в срок
    // выбрасывает; обработчик доставки получает по ним отчёт с ошибкой.
    // close() без аргумента ждал бы бесконечно, и при лежащем брокере
    // сервер студента не завершался бы по Ctrl+C.
    try {
        impl_->producer->close(impl_->options.shutdown_flush);
    } catch (...) {
    }
}

Producer::Producer(Producer&&) noexcept = default;
Producer& Producer::operator=(Producer&&) noexcept = default;

void Producer::on_delivery(std::function<void(const DeliveryReport&)> handler) {
    Impl& impl = alive(impl_);
    std::lock_guard lock(impl.handler_mutex);
    impl.handler = std::move(handler);
}

void Producer::send(std::string_view key, std::string_view payload) {
    Impl* impl = &alive(impl_);

    auto record = mck::ProducerRecord(
        impl->options.topic,
        ::kafka::Key(key.data(), key.size()),
        ::kafka::Value(payload.data(), payload.size()));

    // Ключ нужен в отчёте о доставке, а отчёт придёт позже, когда исходный
    // string_view уже может указывать в никуда. Поэтому копия.
    auto key_copy = std::string(key);

    impl->in_flight.fetch_add(1, std::memory_order_relaxed);

    try {
        impl->producer->send(
            record,
            // Захватываем Impl, а не Producer: при перемещении Producer
            // объект Impl остаётся на месте, и отчёты продолжают приходить.
            [impl, key_copy = std::move(key_copy)](
                const mck::RecordMetadata& metadata, const ::kafka::Error& error) {
                impl->in_flight.fetch_sub(1, std::memory_order_relaxed);

                std::function<void(const DeliveryReport&)> handler;
                {
                    std::lock_guard lock(impl->handler_mutex);
                    handler = impl->handler;
                }
                if (!handler) {
                    return;
                }

                DeliveryReport report;
                report.key = key_copy;
                report.ok = !error;
                if (error) {
                    report.error = error.message();
                } else {
                    report.partition = metadata.partition();
                    report.offset = metadata.offset().value_or(-1);
                }
                handler(report);
            },
            // Библиотека копирует тело сообщения к себе. Без этого буфер,
            // на который смотрит payload, обязан был бы дожить до момента
            // подтверждения — а он почти всегда временный.
            mck::KafkaProducer::SendOption::ToCopyRecordValue,
            // По умолчанию библиотека при полной очереди блокирует
            // вызывающий поток, пока место не освободится, — при лежащем
            // брокере это до 5 минут. Нам нужен немедленный отказ: решать,
            // ждать или терять, должен адаптер студента.
            mck::KafkaProducer::ActionWhileQueueIsFull::NoBlock);
    } catch (const std::exception& e) {
        impl->in_flight.fetch_sub(1, std::memory_order_relaxed);
        detail::rethrow("send", e);
    }
}

std::size_t Producer::pending() const noexcept {
    return impl_ ? impl_->in_flight.load(std::memory_order_relaxed) : 0;
}

bool Producer::flush(std::chrono::milliseconds timeout) {
    Impl& impl = alive(impl_);
    try {
        impl.producer->flush(timeout);
    } catch (const std::exception& e) {
        detail::rethrow("flush", e);
    }
    return pending() == 0;
}

}  // namespace lab::kafka
