#include "taskq/convert.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace taskq {

// SUBMITBATCH очередь [prio=N] тело очередь [prio=N] тело ...
// Тело — один токен: несколько слов берутся в кавычки. Хвоста строки здесь
// нет, иначе было бы непонятно, где кончается одно тело и начинается другое.
// Очередь и приоритет разбирает тот же convert, что и у SUBMIT.
Batch Batch::from_tokens(std::vector<std::string_view> tokens) {
    Batch batch;
    std::size_t i = 0;
    while (i < tokens.size()) {
        const QueueName queue = convert<QueueName>(tokens[i++]);
        int priority = 0;
        if (i < tokens.size() && tokens[i].starts_with(Priority::prefix)) {
            priority = convert<Priority>(tokens[i++].substr(Priority::prefix.size())).value;
        }
        if (i >= tokens.size()) {
            throw ParseError("SUBMITBATCH item for " + std::string(queue.name) + " has no body",
                             std::string(queue.name));
        }
        batch.items.push_back(Item{queue.name, priority, tokens[i++]});
    }
    if (batch.items.empty()) {
        throw ParseError("SUBMITBATCH expects at least one item", "");
    }
    return batch;
}

}  // namespace taskq
