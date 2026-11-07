#include "kvs/commands.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kvs/errors.hpp"
#include "kvs/ivalue.hpp"
#include "kvs/lexer.hpp"
#include "kvs/value.hpp"

namespace kvs {
namespace {

[[nodiscard]] Response ok(std::string text) {
    return Response{true, std::move(text)};
}

// Сложение с проверкой переполнения. Необходимость нашёл фаззер этапа 3:
// переполнение знакового целого — неопределённое поведение.
[[nodiscard]] std::int64_t checked_add(std::string_view key, std::int64_t current, std::int64_t by) {
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();
    if ((by > 0 && current > max - by) || (by < 0 && current < min - by)) {
        throw OverflowError(std::string(key), current, by);
    }
    return current + by;
}

// Значение ключа или исключение — одна точка, где отсутствие ключа
// превращается в ошибку.
[[nodiscard]] Value require(const Store& store, std::string_view key) {
    auto value = store.get(key);
    if (!value) {
        throw KeyNotFoundError(std::string(key));
    }
    return std::move(*value);
}

// Строка ключа или TypeMismatchError. Отсутствующий ключ — пустое значение.
[[nodiscard]] std::string text_of(const Store& store, std::string_view key) {
    const auto value = store.get(key);
    if (!value) {
        return {};
    }
    const auto* const text = value->as<StringValue>();
    if (text == nullptr) {
        throw TypeMismatchError(std::string(key), "string", std::string(value->type_name()));
    }
    return text->data();
}

// --- Обработчики. Первый параметр — хранилище, остальные — аргументы
// команды; реестр разбирает их по типам параметров сам. -------------------

Response set_value(Store& store, std::string_view key, std::string_view value) {
    store.set(std::string(key), Value(value));
    return ok("OK");
}

Response get_value(Store& store, std::string_view key) {
    return ok(require(store, key).to_string());
}

Response delete_key(Store& store, std::string_view key) {
    if (!store.del(key)) {
        throw KeyNotFoundError(std::string(key));
    }
    return ok("OK");
}

Response exists(Store& store, std::string_view key) {
    return ok(store.contains(key) ? "1" : "0");
}

Response type_of(Store& store, std::string_view key) {
    return ok(std::string(require(store, key).type_name()));
}

// INCR key [step]: необязательный аргумент — std::optional, и арность «1 или
// 2» реестр выводит из сигнатуры сам.
Response increment(Store& store, std::string_view key, std::optional<std::int64_t> step) {
    const std::int64_t by = step.value_or(1);
    const auto current = store.get(key);
    if (!current) {
        store.set(std::string(key), Value(by));
        return ok(std::format("{}", by));
    }
    const IntValue* const number = current->as<IntValue>();
    if (number == nullptr) {
        throw TypeMismatchError(std::string(key), "int", std::string(current->type_name()));
    }
    const std::int64_t next = checked_add(key, number->data(), by);
    store.set(std::string(key), Value(next));
    return ok(std::format("{}", next));
}

// MSET k1 v1 k2 v2 ...: пар сколько угодно, поэтому Rest. Чётность — правило
// команды, а не формы аргументов, и проверяется здесь.
Response set_many(Store& store, Rest tokens) {
    if (tokens.tokens.size() % 2 != 0) {
        throw ParseError("MSET expects key-value pairs", std::string(tokens.tokens.back()));
    }
    std::vector<std::pair<std::string, Value>> pairs;
    pairs.reserve(tokens.tokens.size() / 2);
    for (std::size_t i = 0; i < tokens.tokens.size(); i += 2) {
        pairs.emplace_back(std::string(tokens.tokens[i]), Value(tokens.tokens[i + 1]));
    }
    store.mset(pairs);
    return ok("OK");
}

// Список меняется в копии и записывается обратно через set: учёт памяти и
// проверка предела остаются в одном месте, а бросок оставляет хранилище
// прежним. Изменение на месте появится на этапе 7.
Response push_back(Store& store, std::string_view key, std::string_view item) {
    auto current = store.get(key);
    Value list = current ? std::move(*current) : Value(std::make_unique<ListValue>());
    ListValue* const items = list.as<ListValue>();
    if (items == nullptr) {
        throw TypeMismatchError(std::string(key), "list", std::string(list.type_name()));
    }
    items->push_back(std::string(item));
    const std::size_t length = items->length();
    store.set(std::string(key), std::move(list));
    return ok(std::format("{}", length));
}

Response list_length(Store& store, std::string_view key) {
    const Value value = require(store, key);
    const ListValue* const items = value.as<ListValue>();
    if (items == nullptr) {
        throw TypeMismatchError(std::string(key), "list", std::string(value.type_name()));
    }
    return ok(std::format("{}", items->length()));
}

Response list_keys(Store& store, std::optional<std::string_view> pattern,
                   std::optional<std::size_t> limit) {
    const std::vector<std::string> keys =
        store.keys(pattern.value_or("*"), limit.value_or(default_keys_limit));
    if (keys.empty()) {
        return ok("(empty)");
    }
    return ok(std::ranges::to<std::string>(keys | std::views::join_with('\n')));
}

Response version_of(Store& store, std::string_view key) {
    const auto version = store.version(key);
    if (!version) {
        throw KeyNotFoundError(std::string(key));
    }
    return ok(std::format("generation={} timestamp={} author={}", version->generation,
                          version->timestamp, version->author));
}

// --- Домашнее задание этапа 6: три команды, по одной строке регистрации. --

// APPEND key value: дописать к строке, вернуть новую длину. Нет ключа —
// создаётся, как в Redis.
Response append(Store& store, std::string_view key, std::string_view suffix) {
    std::string text = text_of(store, key);
    text += suffix;
    const std::size_t length = text.size();
    store.set(std::string(key), Value(std::move(text)));
    return ok(std::format("{}", length));
}

// STRLEN key: длина строки; нет ключа — 0.
Response string_length(Store& store, std::string_view key) {
    return ok(std::format("{}", text_of(store, key).size()));
}

// GETSET key value: вернуть старое значение и записать новое. Это одна
// операция хранилища — exchange, — а не get и затем set: на этапе 11, когда
// хранилище станет общим для потоков, пара get + set была бы гонкой — между
// ними другой поток успел бы записать своё, и его значение потерялось бы.
Response get_and_set(Store& store, std::string_view key, std::string_view value) {
    const std::optional<Value> old = store.exchange(std::string(key), Value(value));
    return ok(old ? old->to_string() : std::string("(nil)"));
}

}  // namespace

const CommandRegistry<Store>& commands() {
    // Реестр строится один раз, при первом обращении; инициализация
    // локальной статической переменной потокобезопасна по стандарту.
    static const CommandRegistry<Store> registry = [] {
        CommandRegistry<Store> table;
        table.add("SET", Effect::Writes, set_value)
            .add("GET", Effect::Reads, get_value)
            .add("DEL", Effect::Writes, delete_key)
            .add("EXISTS", Effect::Reads, exists)
            .add("TYPE", Effect::Reads, type_of)
            .add("INCR", Effect::Writes, increment)
            .add("MSET", Effect::Writes, set_many)
            .add("RPUSH", Effect::Writes, push_back)
            .add("LLEN", Effect::Reads, list_length)
            .add("KEYS", Effect::Reads, list_keys)
            .add("VERSION", Effect::Reads, version_of)
            .add("APPEND", Effect::Writes, append)
            .add("STRLEN", Effect::Reads, string_length)
            .add("GETSET", Effect::Writes, get_and_set);
        return table;
    }();
    return registry;
}

Response execute(Store& store, std::string_view line) {
    const Tokens tokens = tokenize(line);
    return commands().bind(tokens, line)(store);
}

}  // namespace kvs
