#include "chat/compose.hpp"

#include <memory>

#include "chat/engines.hpp"
#include "chat/log.hpp"

namespace chat {

Server make_server(const Config& config, const IClock& clock) {
    ChatStore store(make_chat_store<ChatStore::Room>(config.engine), config.history_capacity, clock);
    if (!config.log) {
        return Server(std::move(store));
    }
    return Server(std::move(store), std::make_unique<AppendOnlyLog>(*config.log));
}

}  // namespace chat
