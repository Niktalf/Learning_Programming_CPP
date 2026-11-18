#include "kvs/server.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "kvs/commands.hpp"
#include "kvs/errors.hpp"
#include "kvs/reply.hpp"

namespace kvs {

Server::Server(Store store, std::unique_ptr<ILogSink> log, const ILogSource* history)
    : store_(std::move(store)), log_(std::move(log)) {
    if (history == nullptr) {
        return;
    }
    const std::vector<std::string> records = history->records();
    for (std::size_t i = 0; i < records.size(); ++i) {
        // Восстановление — не граница: неудача здесь означает испорченный
        // журнал, и сервер запускаться не должен. Контекст прикладывается тем
        // же std::throw_with_nested, что и при исполнении команды.
        try {
            static_cast<void>(execute(store_, records[i]));
        } catch (...) {
            std::throw_with_nested(KvError("while replaying " + history->name() + " line " +
                                           std::to_string(i + 1)));
        }
    }
}

Response Server::handle(std::string line) {
    try {
        // Владелец запроса — эта функция, пока команда исполняется. Кэш
        // только наблюдает через weak_ptr.
        const std::shared_ptr<const Request> request = cache_.get(line);
        Response response = request->command()(store_);
        // В журнал — только успешные изменения: неудачная команда при
        // восстановлении снова бы не удалась.
        //
        // Ограничение, о котором стоит знать: если запись в журнал не
        // удалась, хранилище уже изменено, а клиент получит ошибку.
        // Журнал до исполнения (write-ahead) этого не допускает, но требует
        // отката по журналу. К этому вопросу курс вернётся на этапе 17,
        // вместе со снапшотами.
        if (log_ && request->command().writes()) {
            log_->append(request->text());
        }
        return response;
    } catch (...) {
        // catch (...) допустим только здесь, на самой внешней границе, и
        // только с осмысленным ответом. Проглотить молча было бы хуже, чем
        // упасть. Формат ответа — дело Reply (этап 9).
        return Reply::error(std::current_exception());
    }
}

}  // namespace kvs
