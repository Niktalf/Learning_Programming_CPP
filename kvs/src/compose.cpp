#include "kvs/compose.hpp"

#include <memory>
#include <optional>

#include "kvs/engines.hpp"
#include "kvs/log.hpp"

namespace kvs {

Server make_server(const Config& config, const IClock& clock) {
    Store store(make_engine<StoreEntry<Value>>(config.engine), config.memory_limit, clock);
    if (!config.log) {
        return Server(std::move(store));
    }
    // История читается до того, как журнал открыт на дозапись: файл,
    // которого ещё нет, — пустая история, а не ошибка.
    const FileHistory history(*config.log);
    return Server(std::move(store), std::make_unique<AppendOnlyLog>(*config.log), &history);
}

}  // namespace kvs
