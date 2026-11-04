#include "kvs/executor.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <variant>

#include "kvs/overloaded.hpp"

namespace kvs {
namespace {

// std::format появится в C++20, пока форматируем потоком.
std::string to_text(std::int64_t value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

}  // namespace

Response execute(Store& store, const Command& command) {
    return std::visit(
        overloaded{
            [&](const SetCommand& c) {
                store.set(std::string(c.key), Value(c.value));
                return Response{true, "OK"};
            },
            [&](const GetCommand& c) {
                // if с инициализацией: результат живёт ровно столько, сколько
                // нужен, и не мешается в остальной части функции.
                if (const auto value = store.get(c.key); value) {
                    return Response{true, value->to_string()};
                }
                return Response{false, "(nil)"};
            },
            [&](const DelCommand& c) {
                return store.del(c.key) ? Response{true, "OK"} : Response{false, "(nil)"};
            },
            [&](const ExistsCommand& c) {
                return Response{true, store.contains(c.key) ? "1" : "0"};
            },
            [&](const TypeCommand& c) {
                if (const auto value = store.get(c.key); value) {
                    return Response{true, std::string(value->type_name())};
                }
                return Response{false, "(nil)"};
            },
            [&](const IncrCommand& c) -> Response {
                const auto value = store.get(c.key);
                if (!value) {
                    store.set(std::string(c.key), Value(c.by));
                    return Response{true, to_text(c.by)};
                }
                // get_if вместо std::get: неверная альтернатива здесь штатная
                // ситуация, а не исключительная. Исключения появятся на этапе 3.
                const std::int64_t* const current = value->get_if<std::int64_t>();
                if (current == nullptr) {
                    return Response{false, "ERR INCR needs an integer, the value is " +
                                               std::string(value->type_name())};
                }
                const std::int64_t next = *current + c.by;
                store.set(std::string(c.key), Value(next));
                return Response{true, to_text(next)};
            },
        },
        command);
}

}  // namespace kvs
