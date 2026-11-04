#include "kvs/executor.hpp"

#include <cstdint>
#include <exception>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "kvs/errors.hpp"
#include "kvs/overloaded.hpp"

namespace kvs {
namespace {

std::string to_text(std::int64_t value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

// Сложение с проверкой переполнения. Нашёл необходимость фаззер из домашнего
// задания: переполнение знакового целого — неопределённое поведение, и без
// проверки INCR с шагом около INT64_MAX давал мусор или ловушку санитайзера.
std::int64_t checked_add(std::string_view key, std::int64_t current, std::int64_t by) {
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();
    if ((by > 0 && current > max - by) || (by < 0 && current < min - by)) {
        throw OverflowError(std::string(key), current, by);
    }
    return current + by;
}

// Короткое описание команды для цепочки контекста: глагол и ключи.
std::string describe(const Command& command) {
    return std::visit(
        overloaded{
            [](const SetCommand& c) { return "SET " + std::string(c.key); },
            [](const GetCommand& c) { return "GET " + std::string(c.key); },
            [](const DelCommand& c) { return "DEL " + std::string(c.key); },
            [](const ExistsCommand& c) { return "EXISTS " + std::string(c.key); },
            [](const TypeCommand& c) { return "TYPE " + std::string(c.key); },
            [](const IncrCommand& c) { return "INCR " + std::string(c.key); },
            [](const MsetCommand& c) {
                std::string text = "MSET";
                for (const auto& [key, value] : c.pairs) {
                    text += ' ';
                    text += key;
                }
                return text;
            },
        },
        command);
}

// Значение ключа или исключение. Одна точка, где отсутствие ключа
// превращается в ошибку, — чтобы не повторять проверку в каждой команде.
Value require(const Store& store, std::string_view key) {
    auto value = store.get(key);
    if (!value) {
        throw KeyNotFoundError(std::string(key));
    }
    return std::move(*value);
}

Response run_command(Store& store, const Command& command) {
    return std::visit(
        overloaded{
            [&](const SetCommand& c) {
                store.set(std::string(c.key), Value(c.value));
                return Response{true, "OK"};
            },
            [&](const GetCommand& c) {
                return Response{true, require(store, c.key).to_string()};
            },
            [&](const DelCommand& c) {
                if (!store.del(c.key)) {
                    throw KeyNotFoundError(std::string(c.key));
                }
                return Response{true, "OK"};
            },
            [&](const ExistsCommand& c) {
                return Response{true, store.contains(c.key) ? "1" : "0"};
            },
            [&](const TypeCommand& c) {
                return Response{true, std::string(require(store, c.key).type_name())};
            },
            [&](const IncrCommand& c) {
                const auto current = store.get(c.key);
                if (!current) {
                    store.set(std::string(c.key), Value(c.by));
                    return Response{true, to_text(c.by)};
                }
                // Этап 2 проверял альтернативу через get_if и возвращал флаг.
                // Теперь неверный тип — ошибка с контекстом в полях: на этапе 16
                // по ней выберется код ответа.
                const std::int64_t* const number = current->get_if<std::int64_t>();
                if (number == nullptr) {
                    throw TypeMismatchError(std::string(c.key), "int",
                                            std::string(current->type_name()));
                }
                const std::int64_t next = checked_add(c.key, *number, c.by);
                store.set(std::string(c.key), Value(next));
                return Response{true, to_text(next)};
            },
            [&](const MsetCommand& c) {
                std::vector<std::pair<std::string, Value>> pairs;
                pairs.reserve(c.pairs.size());
                for (const auto& [key, value] : c.pairs) {
                    pairs.emplace_back(std::string(key), Value(value));
                }
                store.mset(pairs);
                return Response{true, "OK"};
            },
        },
        command);
}

}  // namespace

// Звёздочка этапа 3: цепочка контекста. Ошибка из глубины — из хранилища или
// из проверки типа — не знает, какая команда её вызвала. Исполнитель
// перехватывает её только затем, чтобы приложить это знание, и тут же бросает
// дальше: std::throw_with_nested создаёт новое исключение, внутри которого
// лежит исходное. Ничего не решается и не проглатывается — разворачивает
// цепочку и решает, что ответить, по-прежнему только граница handle.
//
// Это единственный catch внутри ядра, и он не нарушает правило из
// docs/errors.md: постановка запрещает catch (...) без повторного броска,
// а здесь бросок есть всегда.
Response execute(Store& store, const Command& command) {
    try {
        return run_command(store, command);
    } catch (...) {
        std::throw_with_nested(KvError("while executing " + describe(command)));
    }
}

}  // namespace kvs
