#include "taskq/compose.hpp"

#include <memory>
#include <utility>

#include "taskq/engines.hpp"
#include "taskq/log.hpp"

namespace taskq {

Service make_service(const Config& config, const IClock& clock) {
    TaskStore store(make_task_index<TaskStore::IndexEntry>(config.engine), clock);
    std::unique_ptr<ILogSink> log;
    if (config.log) {
        log = std::make_unique<AppendOnlyLog>(*config.log);
    }
    Service service(std::move(store), clock, std::move(log), config.retry);
    service.set_lease_timeout(config.lease_timeout);
    return service;
}

}  // namespace taskq
