#include "chat/compose.hpp"

#include <memory>

#include "chat/engines.hpp"
#include "chat/errors.hpp"
#include "chat/log.hpp"

namespace chat {

namespace {

// Этап 10: издатель событий по конфигурации. Адаптер Kafka собирается
// только с CHAT_KAFKA=ON; без него сервер работает, но событий не шлёт.
[[nodiscard]] std::unique_ptr<IEventPublisher> make_publisher(const Config& config) {
    if (!config.events) {
        return nullptr;
    }
#ifdef CHAT_WITH_KAFKA
    return make_kafka_publisher(*config.events);
#else
    throw ChatError("events requested, but chat was built without Kafka (CHAT_KAFKA=OFF)");
#endif
}

}  // namespace

Server make_server(const Config& config, const IClock& clock) {
    ChatStore store(make_chat_store<ChatStore::Room>(config.engine), config.history_capacity, clock);
    std::unique_ptr<IEventPublisher> events = make_publisher(config);
    std::unique_ptr<ILogSink> log = config.log ? std::make_unique<AppendOnlyLog>(*config.log) : nullptr;
    return Server(std::move(store), std::move(log), std::move(events));
}

}  // namespace chat
