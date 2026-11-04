#include "kvs/server.hpp"

#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "kvs/errors.hpp"
#include "kvs/file.hpp"
#include "kvs/parser.hpp"

namespace kvs {
namespace {

// Ответ по исходной причине ошибки. Порядок веток важен: от частного к
// общему. Если поставить KvError первым, до ParseError очередь не дойдёт
// никогда.
//
// Все ветки ловят по константной ссылке. По значению нельзя: объект
// обрезался бы до базового класса, и поля потомка потерялись бы.
[[nodiscard]] std::string reason_of(const std::exception_ptr& cause) {
    try {
        std::rethrow_exception(cause);
    } catch (const ParseError& e) {
        return e.reason();
    } catch (const KeyNotFoundError& e) {
        return "no such key: " + e.key();
    } catch (const TypeMismatchError& e) {
        return "wrong type: expected " + e.expected() + ", got " + e.actual();
    } catch (const OutOfMemoryError& e) {
        return "out of memory: " + std::to_string(e.required()) + " bytes required, limit is " +
               std::to_string(e.limit());
    } catch (const OverflowError&) {
        return "increment would overflow";
    } catch (const KvError& e) {
        return e.what();
    } catch (const std::bad_alloc&) {
        return "out of memory (allocation failed)";
    } catch (const std::exception& e) {
        return std::string("internal: ") + e.what();
    } catch (...) {
        return "internal: unknown error";
    }
}

[[nodiscard]] Store make_store(const std::optional<std::size_t>& limit) {
    return limit ? Store(*limit) : Store();
}

}  // namespace

Server::Server(const Options& options) : store_(make_store(options.memory_limit)) {
    if (!options.log) {
        return;
    }
    const std::filesystem::path& path = *options.log;
    if (std::filesystem::exists(path)) {
        const std::vector<std::string> records = read_lines(path);
        for (std::size_t i = 0; i < records.size(); ++i) {
            // Восстановление — не граница: неудача здесь означает
            // испорченный журнал, и сервер запускаться не должен. Контекст
            // прикладывается тем же std::throw_with_nested, что и в execute.
            try {
                const InputLine line{records[i]};
                static_cast<void>(execute(store_, parse(line)));
            } catch (...) {
                std::throw_with_nested(KvError("while replaying " + path.string() + " line " +
                                               std::to_string(i + 1)));
            }
        }
    }
    log_.emplace(path);
}

Response Server::handle(std::string line) {
    try {
        // Владелец запроса — эта функция, пока команда исполняется. Кэш
        // только наблюдает через weak_ptr.
        const std::shared_ptr<const Request> request = cache_.get(line);
        Response response = execute(store_, request->command());
        // В журнал — только успешные изменения: неудачная команда при
        // восстановлении снова бы не удалась.
        //
        // Ограничение, о котором стоит знать: если запись в журнал не
        // удалась, хранилище уже изменено, а клиент получит ошибку.
        // Журнал до исполнения (write-ahead) этого не допускает, но требует
        // отката по журналу. К этому вопросу курс вернётся на этапе 17,
        // вместе со снапшотами.
        if (log_ && modifies(request->command())) {
            log_->append(request->text());
        }
        return response;
    } catch (...) {
        // catch (...) допустим только здесь, на самой внешней границе, и
        // только с осмысленным ответом. Проглотить молча было бы хуже, чем
        // упасть.
        //
        // Ответ строится по исходной причине, а цепочка контекста, если она
        // есть, приписывается в скобках: «ERR no such key: k (while executing
        // GET k)». Клиенту видно и что случилось, и на какой команде.
        const std::exception_ptr error = std::current_exception();
        std::string text = "ERR " + reason_of(root_cause(error));
        for (const std::string& step : context_of(error)) {
            text += " (" + step + ")";
        }
        return Response{false, std::move(text)};
    }
}

}  // namespace kvs
