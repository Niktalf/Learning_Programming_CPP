// Этап 7, пункт 3: индекс комнат и индекс ников на std::map и
// std::unordered_map.
//
//   cmake --build build-release --target chat_bench_tables
//   ./build-release/chat_bench_tables
//
// Для каждого размера — 10^3, 10^5, 10^6 записей — три операции:
//   - вставка всех ключей в случайном порядке;
//   - поиск каждого ключа, тоже в случайном порядке, все поиски успешные;
//   - обход в алфавитном порядке — так комнаты выдаёт /rooms. У map это
//     просто обход; unordered_map порядка не хранит, и имена приходится
//     собрать и отсортировать.
// Отдельно — худший случай unordered_map: все ключи с одним хешем.
//
// Значение в таблице — 8-байтное число: меряется таблица, а не значения.
// Имена — комнаты #room-1000123 и ники user-1000123: 13–14 символов, короче
// 16, и строки, как и настоящие имена (до 32 символов, обычно короткие),
// живут внутри std::string без выделений.
// Результаты и выводы — в docs/benchmarks.md.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "bench_util.hpp"

namespace {

std::vector<std::string> keys_of(std::size_t count) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        keys.push_back("#room-" + std::to_string(1000000 + i));
    }
    std::shuffle(keys.begin(), keys.end(), std::mt19937_64(20261025));
    return keys;
}

// Хеш, дающий всем ключам одно значение. Так выглядит атака на хеш-таблицу:
// std::hash<std::string> в libstdc++ без случайной затравки, и ключи с
// одинаковым хешем можно подобрать заранее. Здесь совпадение подстроено, но
// таблица ведёт себя так же: все ключи в одной корзине, и каждый поиск —
// линейный проход по ней.
struct Colliding {
    std::size_t operator()(std::string_view) const noexcept { return 42; }
};

template <class Table>
void insert_all(Table& table, const std::vector<std::string>& keys) {
    for (std::size_t i = 0; i < keys.size(); ++i) {
        table.try_emplace(keys[i], i);
    }
}

template <class Table>
void find_all(const Table& table, const std::vector<std::string>& keys) {
    std::size_t found = 0;
    for (const std::string& key : keys) {
        found += table.find(key) != table.end() ? 1 : 0;
    }
    chat::bench::keep(found);
}

template <class Table>
void ordered_walk(const Table& table) {
    std::size_t total = 0;
    if constexpr (requires { table.key_comp(); }) {
        for (const auto& [key, value] : table) {
            total += key.size() + value;
        }
    } else {
        // Порядка нет — собрать указатели на имена и отсортировать. Это и
        // есть цена /rooms поверх unordered_map.
        std::vector<const std::string*> sorted;
        sorted.reserve(table.size());
        for (const auto& [key, value] : table) {
            sorted.push_back(&key);
        }
        std::ranges::sort(sorted, {}, [](const std::string* key) { return *key; });
        for (const std::string* const key : sorted) {
            total += key->size();
        }
    }
    chat::bench::keep(total);
}

template <class Table>
void report(const char* name, std::size_t count) {
    const auto keys = keys_of(count);
    const int rounds = count >= 1000000 ? 3 : 5;
    const auto insert = chat::bench::measure(
        rounds, [] { return Table{}; }, [&](Table& table) { insert_all(table, keys); });
    Table filled;
    insert_all(filled, keys);
    const auto find = chat::bench::measure(
        rounds, [] { return 0; }, [&](int) { find_all(filled, keys); });
    const auto walk = chat::bench::measure(
        rounds, [] { return 0; }, [&](int) { ordered_walk(filled); });
    std::cout << "| " << name << " | " << count << " | " << insert.milliseconds << " | "
              << insert.allocations << " | " << find.milliseconds << " | " << walk.milliseconds
              << " |\n";
}

}  // namespace

int main() {
    using Ordered = std::map<std::string, std::uint64_t, std::less<>>;
    using Hashed = std::unordered_map<std::string, std::uint64_t>;
    using Attacked = std::unordered_map<std::string, std::uint64_t, Colliding>;

    std::cout << "| table | keys | insert, ms | insert allocations | find all, ms | ordered walk, ms |\n"
              << "|---|---|---|---|---|---|\n";
    for (const std::size_t count : {1000u, 100000u, 1000000u}) {
        report<Ordered>("map", count);
        report<Hashed>("unordered_map", count);
    }
    // Худший случай квадратичен: на 10^6 его не дождаться, поэтому размеры
    // меньше, и для сравнения — map на тех же размерах.
    std::cout << "\nworst case: every key has the same hash\n\n"
              << "| table | keys | insert, ms | insert allocations | find all, ms | ordered walk, ms |\n"
              << "|---|---|---|---|---|---|\n";
    for (const std::size_t count : {1000u, 10000u, 30000u}) {
        report<Ordered>("map", count);
        report<Attacked>("unordered_map, one hash", count);
    }
    return 0;
}
