#include "executor.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "errors.hpp"
#include "overloaded.hpp"

namespace {
    std::string to_text(const std::int64_t value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

    Value require(const Store& store, const std::string_view key) {
        auto value = store.get(key);
        if (!value) {
            throw KeyNotFoundError(std::string(key));
        }
        return std::move(*value);
    }
}  // namespace

Response execute(Store& store, const Command& command) {
    return std::visit(
        overloaded{
            [&](const SetCommand& c) {
                store.set(std::string(c.key), Value(c.value));
                return Response{
                    .ok = true,
                    .text = "OK"
                };
            },
            [&](const GetCommand& c) {
                return Response{
                    .ok = true,
                    .text = require(store, c.key).to_string()
                };
            },
            [&](const DelCommand& c) {
                if (!store.del(c.key)) {
                    throw KeyNotFoundError(std::string(c.key));
                }
                return Response{
                    .ok = true,
                    .text = "OK"
                };
            },
            [&](const ExistsCommand& c) {
                return Response{
                    .ok = true,
                    .text = store.contains(c.key) ? "1" : "0"
                };
            },
            [&](const TypeCommand& c) {
                return Response{
                    .ok = true,
                    .text = std::string(require(store, c.key).type_name())
                };
            },
            [&](const IncrCommand& c) {
                const auto current = store.get(c.key);
                if (!current) {
                    store.set(std::string(c.key), Value(c.by));
                    return Response{
                        .ok = true,
                        .text = to_text(c.by)
                    };
                }
                const auto* number = current->get_if<std::int64_t>();
                if (number == nullptr) {
                    throw TypeMismatchError(std::string(c.key), "int",
                        std::string(current->type_name()));
                }
                const std::int64_t next = *number + c.by;
                store.set(std::string(c.key), Value(next));
                return Response{
                    .ok = true,
                    .text = to_text(next)
                };
            },
            [&](const MsetCommand& c) {
                std::vector<std::pair<std::string, Value>> pairs;
                pairs.reserve(c.pairs.size());
                for (const auto& [key, value] : c.pairs) {
                    pairs.emplace_back(std::string(key), Value(value));
                }
                store.mset(pairs);
                return Response{
                    .ok = true,
                    .text = "OK"
                };
            },
        },
        command);
}
