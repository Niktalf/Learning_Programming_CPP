#include "kvs/commands.hpp"

#include <algorithm>
#include <chrono>
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
#include "kvs/reply.hpp"
#include "kvs/value.hpp"

namespace kvs {
namespace {

// Этап 9: формат ответа — дело Reply (reply.hpp, Pure Fabrication).
[[nodiscard]] Response ok(std::string text) {
    return Reply::ok(std::move(text));
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
// превращается в ошибку. Этап 7: ссылка, а не копия (Store::lookup) — до
// этапа 7 каждый GET копировал значение целиком.
[[nodiscard]] const Value& require(const Store& store, std::string_view key) {
    const auto value = store.lookup(key);
    if (!value) {
        throw KeyNotFoundError(std::string(key));
    }
    return value->get();
}

// Строка ключа или TypeMismatchError. Отсутствующий ключ — пустое значение.
[[nodiscard]] std::string text_of(const Store& store, std::string_view key) {
    const auto value = store.lookup(key);
    if (!value) {
        return {};
    }
    const auto* const text = value->get().as<StringValue>();
    if (text == nullptr) {
        throw TypeMismatchError(std::string(key), "string", std::string(value->get().type_name()));
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

// EXISTS key [key ...]: сколько из названных ключей есть в хранилище, как в
// Redis. Этап 8: подсчёт — count_if, а не цикл со счётчиком. Повтор ключа
// считается повторно — тоже как в Redis.
Response exists(Store& store, std::string_view first, Rest more) {
    const auto present = [&store](std::string_view key) { return store.contains(key); };
    const auto count = (present(first) ? 1 : 0) + std::ranges::count_if(more.tokens, present);
    return ok(std::to_string(count));
}

Response type_of(Store& store, std::string_view key) {
    return ok(std::string(require(store, key).type_name()));
}

// INCR key [step]: необязательный аргумент — std::optional, и арность «1 или
// 2» реестр выводит из сигнатуры сам.
Response increment(Store& store, std::string_view key, std::optional<std::int64_t> step) {
    const std::int64_t by = step.value_or(1);
    const auto current = store.lookup(key);
    if (!current) {
        store.set(std::string(key), Value(by));
        return ok(std::format("{}", by));
    }
    const IntValue* const number = current->get().as<IntValue>();
    if (number == nullptr) {
        throw TypeMismatchError(std::string(key), "int", std::string(current->get().type_name()));
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
    // Этап 8: views::chunk(2) из C++23 режет токены на пары — вместо
    // индекса с шагом 2.
    for (const auto pair : tokens.tokens | std::views::chunk(2)) {
        // Этап 7: emplace_back из двух string_view, а не из временных
        // std::string и Value. С временными пара строилась бы из них
        // перемещением — ещё два объекта и два перемещения на каждую пару;
        // так строка и значение рождаются прямо в элементе вектора
        // (tests/test_emplace.cpp показывает разницу на Traced).
        pairs.emplace_back(pair[0], pair[1]);
    }
    store.mset(pairs);
    return ok("OK");
}

// --- Этап 7: списки ------------------------------------------------------
//
// Список меняется на месте, через Store::modify: до этапа 7 RPUSH копировал
// весь список ради одного элемента. Ключа нет — новый список записывается
// через set одной операцией: создать пустой и потом дописать — две записи, и
// если вторая упрётся в предел памяти, останется пустой список.

[[nodiscard]] ListValue& as_list(std::string_view key, Value& value) {
    ListValue* const items = value.as<ListValue>();
    if (items == nullptr) {
        throw TypeMismatchError(std::string(key), "list", std::string(value.type_name()));
    }
    return *items;
}

[[nodiscard]] const ListValue& as_list(std::string_view key, const Value& value) {
    const ListValue* const items = value.as<ListValue>();
    if (items == nullptr) {
        throw TypeMismatchError(std::string(key), "list", std::string(value.type_name()));
    }
    return *items;
}

enum class End { Front, Back };

Response push(Store& store, std::string_view key, std::string_view item, End end) {
    if (!store.contains(key)) {
        auto created = std::make_unique<ListValue>();
        created->push_back(std::string(item));
        store.set(std::string(key), Value(std::move(created)));
        return ok("1");
    }
    const std::size_t length = store.modify(key, item.size(), [&](Value& value) {
        ListValue& items = as_list(key, value);
        if (end == End::Front) {
            items.push_front(std::string(item));
        } else {
            items.push_back(std::string(item));
        }
        return items.length();
    });
    return ok(std::format("{}", length));
}

// Снятие последнего элемента удаляет ключ, как в Redis: пустых списков в
// хранилище не бывает, и TYPE пустого списка не нужно придумывать.
Response pop(Store& store, std::string_view key, End end) {
    if (!store.contains(key)) {
        return ok(std::string(Reply::nil));
    }
    auto [item, now_empty] = store.modify(key, 0, [&](Value& value) {
        ListValue& items = as_list(key, value);
        std::string taken = end == End::Front ? items.pop_front() : items.pop_back();
        return std::pair{std::move(taken), items.empty()};
    });
    if (now_empty) {
        static_cast<void>(store.del(key));
    }
    return ok(std::move(item));
}

Response push_front(Store& store, std::string_view key, std::string_view item) {
    return push(store, key, item, End::Front);
}

Response push_back(Store& store, std::string_view key, std::string_view item) {
    return push(store, key, item, End::Back);
}

Response pop_front(Store& store, std::string_view key) {
    return pop(store, key, End::Front);
}

Response pop_back(Store& store, std::string_view key) {
    return pop(store, key, End::Back);
}

Response list_length(Store& store, std::string_view key) {
    return ok(std::format("{}", as_list(key, require(store, key)).length()));
}

// LRANGE key start stop: индексы как у Redis — отрицательные от конца,
// LRANGE key 0 -1 — весь список.
Response list_range(Store& store, std::string_view key, std::int64_t start, std::int64_t stop) {
    const auto value = store.lookup(key);
    if (!value) {
        return ok(std::string(Reply::empty_list));
    }
    return Reply::lines(as_list(key, value->get()).range(start, stop));
}

Response list_keys(Store& store, std::optional<std::string_view> pattern,
                   std::optional<std::size_t> limit) {
    return Reply::lines(store.keys(pattern.value_or("*"), limit.value_or(default_keys_limit)));
}

// --- Этап 8: SCAN и KEYSRANGE --------------------------------------------

// SCAN cursor [pattern] [count]: первая строка ответа — курсор следующей
// страницы, 0 — конец обхода; дальше — ключи страницы. count — сколько
// корзин посмотреть за вызов, по умолчанию 10.
Response scan_keys(Store& store, std::size_t cursor, std::optional<std::string_view> pattern,
                   std::optional<std::size_t> count) {
    const auto page = store.scan_page(cursor, count.value_or(10), pattern.value_or("*"));
    // Курсор — первой строкой, ключи — следующими: курсор впереди всего
    // диапазона, и склейка — Reply::lines.
    const std::string cursor_line = std::format("{}", page.next_cursor);
    std::vector<std::string_view> lines{cursor_line};
    lines.insert(lines.end(), page.keys.begin(), page.keys.end());
    return Reply::lines(lines);
}

// KEYSRANGE from to [limit]: ключи от from до to включительно в порядке
// строк — по упорядоченному индексу.
Response keys_range(Store& store, std::string_view from, std::string_view to,
                    std::optional<std::size_t> limit) {
    return Reply::lines(store.keys_range(from, to, limit.value_or(default_keys_limit)));
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
    return ok(old ? old->to_string() : std::string(Reply::nil));
}

// --- Этап 9 ---------------------------------------------------------------

// MEMORY: занятый объём и предел. До этапа 9 эту команду обрабатывал
// консольный клиент сам, мимо реестра, дотягиваясь до хранилища через
// сервер (закон Деметры, docs/audit.md, запись 8): по сети её не было бы
// вовсе. Текст ответа прежний.
Response memory(Store& store) {
    const auto limit = store.memory_limit();
    return ok(std::format("used {} bytes, limit {}", store.used_bytes(),
                          limit ? std::format("{} bytes", *limit) : std::string("none")));
}

// IDLETIME key: секунд с последней записи ключа, как OBJECT IDLETIME у
// Redis. Время — от часов хранилища (IClock): тест сдвигает подставные часы
// и получает ответ мгновенно.
Response idle_time(Store& store, std::string_view key) {
    const auto idle = store.idle_time(key);
    if (!idle) {
        throw KeyNotFoundError(std::string(key));
    }
    return ok(std::format("{}", std::chrono::duration_cast<std::chrono::seconds>(*idle).count()));
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
            .add("LPUSH", Effect::Writes, push_front)
            .add("RPUSH", Effect::Writes, push_back)
            .add("LPOP", Effect::Writes, pop_front)
            .add("RPOP", Effect::Writes, pop_back)
            .add("LRANGE", Effect::Reads, list_range)
            .add("LLEN", Effect::Reads, list_length)
            .add("KEYS", Effect::Reads, list_keys)
            .add("SCAN", Effect::Reads, scan_keys)
            .add("KEYSRANGE", Effect::Reads, keys_range)
            .add("VERSION", Effect::Reads, version_of)
            .add("APPEND", Effect::Writes, append)
            .add("STRLEN", Effect::Reads, string_length)
            .add("GETSET", Effect::Writes, get_and_set)
            .add("MEMORY", Effect::Reads, memory)
            .add("IDLETIME", Effect::Reads, idle_time);
        return table;
    }();
    return registry;
}

Response execute(Store& store, std::string_view line) {
    const Tokens tokens = tokenize(line);
    return commands().bind(tokens, line)(store);
}

}  // namespace kvs
