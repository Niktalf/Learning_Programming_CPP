// Этап 8, пункт 6: один и тот же замер на каждом движке хранилища.
//
//   ./build-release/kvs_bench_engines
//
// Через интерфейс IStorageEngine — так, как с движком работает хранилище:
// вставка N ключей, поиск каждого в случайном порядке, поиск N
// отсутствующих, обход, удаление всех. Ключи — «user:profile:N», 21 символ,
// как в замере этапа 7. Значение — 16 байт: меряется таблица.
//
// Результаты и честный вывод — docs/benchmarks.md.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "bench_util.hpp"
#include "kvs/engine.hpp"

namespace {

struct Entry {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
};

std::vector<std::string> keys_of(std::size_t count, std::string_view prefix) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        keys.push_back(std::string(prefix) + std::to_string(1000000 + i));
    }
    std::shuffle(keys.begin(), keys.end(), std::mt19937_64(20261025));
    return keys;
}

void report(kvs::EngineKind kind, std::size_t count) {
    using Engine = std::unique_ptr<kvs::IStorageEngine<Entry>>;
    const auto keys = keys_of(count, "user:profile:");
    const auto absent = keys_of(count, "user:missing:");
    const int rounds = count >= 1000000 ? 3 : 5;

    const auto insert = kvs::bench::measure(
        rounds, [&] { return kvs::make_engine<Entry>(kind); },
        [&](Engine& engine) {
            for (const std::string& key : keys) {
                engine->put(key, Entry{key.size(), 0});
            }
        });
    Engine filled = kvs::make_engine<Entry>(kind);
    for (const std::string& key : keys) {
        filled->put(key, Entry{key.size(), 0});
    }
    const auto hit = kvs::bench::measure(rounds, [] { return 0; }, [&](int) {
        std::uint64_t sum = 0;
        for (const std::string& key : keys) {
            sum += filled->find(key)->a;
        }
        kvs::bench::keep(sum);
    });
    const auto miss = kvs::bench::measure(rounds, [] { return 0; }, [&](int) {
        std::size_t found = 0;
        for (const std::string& key : absent) {
            found += filled->contains(key) ? 1 : 0;
        }
        kvs::bench::keep(found);
    });
    const auto walk = kvs::bench::measure(rounds, [] { return 0; }, [&](int) {
        std::size_t total = 0;
        for (auto slot = filled->first_from(0); slot; slot = filled->next(*slot)) {
            total += filled->key_at(*slot).size();
        }
        kvs::bench::keep(total);
    });
    const auto erase = kvs::bench::measure(
        rounds,
        [&] {
            Engine engine = filled->clone();
            return engine;
        },
        [&](Engine& engine) {
            for (const std::string& key : keys) {
                engine->erase(key);
            }
        });
    std::cout << "| " << kvs::name_of(kind) << " | " << count << " | " << insert.milliseconds << " | "
              << insert.allocations << " | " << hit.milliseconds << " | " << miss.milliseconds << " | "
              << walk.milliseconds << " | " << erase.milliseconds << " |\n";
}

}  // namespace

int main() {
    std::cout << "| engine | keys | insert, ms | allocations | find hit, ms | find miss, ms | walk, ms | erase, ms |\n"
              << "|---|---|---|---|---|---|---|---|\n";
    for (const std::size_t count : {1000u, 100000u, 1000000u}) {
        for (const kvs::EngineKind kind : {kvs::EngineKind::Std, kvs::EngineKind::Hash, kvs::EngineKind::Open}) {
            report(kind, count);
        }
    }
    // Плохая хеш-функция — отдельно и на меньших размерах: на 10^6 её не
    // дождаться.
    for (const std::size_t count : {1000u, 10000u, 30000u}) {
        report(kvs::EngineKind::Hash, count);
        report(kvs::EngineKind::BadHash, count);
    }
    return 0;
}
