#include "kvs/compose.hpp"

#include <memory>
#include <optional>

#include "kvs/engines.hpp"
#include "kvs/errors.hpp"
#include "kvs/log.hpp"

namespace kvs {

namespace {

// Этап 10: издатель событий по конфигурации. Адаптер Kafka собирается
// только с KVS_KAFKA=ON; без него сервер работает, но событий не шлёт.
[[nodiscard]] std::unique_ptr<IEventPublisher> make_publisher(const Config& config) {
    if (!config.events) {
        return nullptr;
    }
#ifdef KVS_WITH_KAFKA
    return make_kafka_publisher(*config.events);
#else
    throw KvError("events requested, but kvs was built without Kafka (KVS_KAFKA=OFF)");
#endif
}

}  // namespace

Server make_server(const Config& config, const IClock& clock) {
    Store store(make_engine<StoreEntry<Value>>(config.engine), config.memory_limit, clock);
    std::unique_ptr<IEventPublisher> events = make_publisher(config);
    if (!config.log) {
        return Server(std::move(store), nullptr, nullptr, std::move(events));
    }
    // История читается до того, как журнал открыт на дозапись: файл,
    // которого ещё нет, — пустая история, а не ошибка.
    const FileHistory history(*config.log);
    return Server(std::move(store), std::make_unique<AppendOnlyLog>(*config.log), &history, std::move(events));
}

}  // namespace kvs
