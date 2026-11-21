#include "kvs/reply.hpp"

#include <algorithm>
#include <exception>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "kvs/errors.hpp"

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

}  // namespace

Response Reply::error(const std::exception_ptr& error) {
    // Свёртка: каждое звено контекста дописывается к ответу в скобках.
    std::string text = std::ranges::fold_left(context_of(error), "ERR " + reason_of(root_cause(error)),
                                              [](std::string acc, const std::string& step) {
                                                  return std::move(acc) + " (" + step + ")";
                                              });
    return Response{false, std::move(text)};
}

}  // namespace kvs
