// lab/kafka/event_bus.hpp
//
// Тонкая обёртка над клиентом Kafka для практикума.
// Выдаётся студентам готовой: на этапе 10 они пишут только адаптер,
// который превращает доменное событие в пару «ключ — тело» и отдаёт её сюда.
//
// Здесь намеренно нет ничего доменного. Обёртка одна на все три варианта
// проекта и не знает ни про ключи хранилища, ни про комнаты чата,
// ни про задачи. Поэтому и топик у неё не имеет значения по умолчанию:
// каждый вариант пишет в свой (kvs.events, chat.events, taskq.events).
//
// Требует C++20: параметры передаются назначенными инициализаторами.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lab::kafka {

// Все ошибки обёртки. Ловится по константной ссылке, как всё остальное в курсе.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------- Producer

struct ProducerOptions {
    // Обязательный параметр: пустой топик — Error в конструкторе.
    std::string topic;

    std::string brokers = "localhost:9092";
    std::string client_id = "lab";

    // Сколько деструктор ждёт отправки неподтверждённых сообщений.
    // Не уложившиеся в срок выбрасываются, и обработчик доставки получает
    // по каждому отчёт с ошибкой. Поэтому сервер завершается быстро даже
    // при лежащем брокере.
    std::chrono::milliseconds shutdown_flush{5000};

    // Верхняя граница внутренней очереди. При переполнении send немедленно
    // бросает Error — не блокируется и не ждёт брокера.
    // Это и есть та развилка из домашнего задания этапа 10: очередь не может
    // расти бесконечно, и вы обязаны решить, что делать при её переполнении —
    // ждать, терять события или отказывать клиенту.
    std::size_t max_queued_messages = 100000;
};

struct DeliveryReport {
    std::string key;
    bool ok = false;
    std::string error;              // пусто, если ok
    std::int32_t partition = -1;
    std::int64_t offset = -1;
};

class Producer {
public:
    explicit Producer(ProducerOptions options);
    ~Producer();

    Producer(const Producer&) = delete;
    Producer& operator=(const Producer&) = delete;
    Producer(Producer&&) noexcept;
    Producer& operator=(Producer&&) noexcept;

    // Ставит сообщение в очередь и возвращает управление немедленно.
    // Сетевых операций в вызывающем потоке не выполняет и не блокируется,
    // даже если брокер недоступен: сообщение ждёт в очереди, клиент сам
    // повторяет отправку. Если брокер не вернётся за 5 минут, придёт отчёт
    // с ошибкой.
    //
    // Ключ определяет партицию: сообщения с одинаковым ключом попадают в одну
    // партицию, и их относительный порядок сохраняется. Порядок между разными
    // ключами не гарантирован ничем. Пустой ключ — партицию выбирает клиент.
    //
    // Бросает Error, если очередь переполнена.
    void send(std::string_view key, std::string_view payload);

    // Обработчик подтверждений. Вызывается из фонового потока библиотеки,
    // поэтому всё, к чему он обращается, должно быть потокобезопасным.
    // Устанавливать до первого вызова send.
    void on_delivery(std::function<void(const DeliveryReport&)> handler);

    // Сколько сообщений отправлено, но ещё не подтверждено брокером.
    std::size_t pending() const noexcept;

    // Ждёт опустошения очереди. Возвращает true, если успела опустеть.
    bool flush(std::chrono::milliseconds timeout);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------- Consumer

struct ConsumerOptions {
    // Обязательный параметр: пустой топик — Error в конструкторе.
    std::string topic;

    std::string brokers = "localhost:9092";
    std::string group_id = "lab-tail";

    // Читать ли топик с начала, если у группы ещё нет сохранённого смещения.
    bool from_beginning = true;

    // Автоматическая фиксация смещений. Выключите, если делаете звёздочку
    // про идемпотентного потребителя: там смещение надо фиксировать вручную,
    // после того как событие действительно обработано.
    bool auto_commit = true;

    // Конструктор ждёт, пока брокер назначит потребителю партиции. Обычно это
    // занимает 1–3 секунды; если тот же потребитель перед этим был убит без
    // закрытия — до 6–8 секунд. Если брокер не ответил за это время — Error.
    std::chrono::milliseconds join_timeout{15000};
};

struct Record {
    std::string key;
    std::string value;
    std::int32_t partition = -1;
    std::int64_t offset = -1;
    std::chrono::system_clock::time_point timestamp{};
};

class Consumer {
public:
    explicit Consumer(ConsumerOptions options);
    ~Consumer();

    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;
    Consumer(Consumer&&) noexcept;
    Consumer& operator=(Consumer&&) noexcept;

    // Забирает накопленные сообщения. Пустой вектор — штатный результат:
    // он означает «за отведённое время ничего не пришло», а не ошибку.
    std::vector<Record> poll(std::chrono::milliseconds timeout);

    // Фиксирует смещения всего, что вернул последний poll.
    // Имеет смысл только при auto_commit == false.
    void commit();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lab::kafka
