// Этап 7, пункт 3: индекс задач по номеру на std::map и std::unordered_map.
//
//   cmake --build build-release --target taskq_bench_index
//   ./build-release/taskq_bench_index
//
// Для каждого размера — 10^3, 10^5, 10^6 задач — три операции:
//   - вставка всех номеров по возрастанию — так их выдаёт сервис;
//   - поиск каждого номера в случайном порядке — STATUS, ACK, BUMP;
//   - обход всего индекса — REAP, проверки инвариантов.
// Отдельно — худший случай unordered_map: все номера с одним хешем.
//
// Значение — 16 байт, как запись индекса: где лежит задача.
// Результаты и выводы — в docs/benchmarks.md.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

#include "bench_util.hpp"

namespace {

using Id = std::uint64_t;

struct Location {
    std::uint64_t queue;
    std::uint64_t position;
};

// Все номера в одну корзину. Для номеров задач, в отличие от строковых
// ключей KVS, такое не подобрать снаружи: номера выдаёт сервис, подряд,
// и клиент их не выбирает. Замер показывает цену деградации, а не риск.
struct Colliding {
    std::size_t operator()(Id) const noexcept { return 42; }
};

template <class Index>
void insert_all(Index& index, std::size_t count) {
    for (Id id = 1; id <= count; ++id) {
        index.try_emplace(id, Location{id % 7, id});
    }
}

template <class Index>
void find_all(const Index& index, const std::vector<Id>& order) {
    std::uint64_t sum = 0;
    for (const Id id : order) {
        sum += index.find(id)->second.position;
    }
    taskq::bench::keep(sum);
}

template <class Index>
void walk(const Index& index) {
    std::uint64_t sum = 0;
    for (const auto& [id, location] : index) {
        sum += id ^ location.queue;
    }
    taskq::bench::keep(sum);
}

template <class Index>
void report(const char* name, std::size_t count) {
    std::vector<Id> order(count);
    for (std::size_t i = 0; i < count; ++i) {
        order[i] = i + 1;
    }
    std::shuffle(order.begin(), order.end(), std::mt19937_64(20261025));
    const int rounds = count >= 1000000 ? 3 : 5;
    const auto insert = taskq::bench::measure(
        rounds, [] { return Index{}; }, [&](Index& index) { insert_all(index, count); });
    Index filled;
    insert_all(filled, count);
    const auto find = taskq::bench::measure(rounds, [] { return 0; }, [&](int) { find_all(filled, order); });
    const auto all = taskq::bench::measure(rounds, [] { return 0; }, [&](int) { walk(filled); });
    std::cout << "| " << name << " | " << count << " | " << insert.milliseconds << " | " << insert.allocations
              << " | " << find.milliseconds << " | " << all.milliseconds << " |\n";
}

}  // namespace

int main() {
    using Ordered = std::map<Id, Location>;
    using Hashed = std::unordered_map<Id, Location>;
    using Attacked = std::unordered_map<Id, Location, Colliding>;

    std::cout << "| index | tasks | insert, ms | insert allocations | find all, ms | walk, ms |\n"
              << "|---|---|---|---|---|---|\n";
    for (const std::size_t count : {1000u, 100000u, 1000000u}) {
        report<Ordered>("map", count);
        report<Hashed>("unordered_map", count);
    }
    std::cout << "\nworst case: every id has the same hash\n\n"
              << "| index | tasks | insert, ms | insert allocations | find all, ms | walk, ms |\n"
              << "|---|---|---|---|---|---|\n";
    for (const std::size_t count : {1000u, 10000u, 30000u}) {
        report<Ordered>("map", count);
        report<Attacked>("unordered_map, one hash", count);
    }
    return 0;
}
