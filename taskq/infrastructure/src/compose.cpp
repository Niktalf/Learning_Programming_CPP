#include "taskq/compose.hpp"

#include <memory>
#include <utility>

#include "taskq/engines.hpp"
#include "taskq/errors.hpp"
#include "taskq/log.hpp"

namespace taskq {

namespace {

// Этап 10: издатель событий по конфигурации. Адаптер Kafka собирается
// только с TASKQ_KAFKA=ON; без него сервис работает, но событий не шлёт.
[[nodiscard]] std::unique_ptr<IEventPublisher> make_publisher(const Config& config) {
    if (!config.events) {
        return nullptr;
    }
#ifdef TASKQ_WITH_KAFKA
    return make_kafka_publisher(*config.events);
#else
    throw TaskQError("events requested, but taskq was built without Kafka (TASKQ_KAFKA=OFF)");
#endif
}

}  // namespace

Service make_service(const Config& config, const IClock& clock) {
    TaskStore store(make_task_index<TaskStore::IndexEntry>(config.engine), clock);
    std::unique_ptr<ILogSink> log;
    if (config.log) {
        log = std::make_unique<AppendOnlyLog>(*config.log);
    }
    Service service(std::move(store), clock, std::move(log), config.retry, make_publisher(config));
    service.set_lease_timeout(config.lease_timeout);
    return service;
}

}  // namespace taskq
