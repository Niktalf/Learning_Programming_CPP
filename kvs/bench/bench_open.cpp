// Звёздочка этапа 8: цепочки против открытой адресации при коэффициенте
// заполнения 0.5, 0.75 и 0.9.
//
//   ./build-release/kvs_bench_open
//
// Таблица создаётся сразу нужного размера — 2^20 ячеек — с порогом выше
// единицы, чтобы она не росла сама, и заполняется ровно до заданного α.
// Затем: успешный поиск каждого ключа, неуспешный поиск стольких же
// отсутствующих и «текучка» — удаление и вставка нового ключа, как ники в
// чате. Рядом — измеренная средняя длина пробега, теоретическая оценка
// Кнута для линейного пробирования — успешный поиск ½(1 + 1/(1-α)),
// неуспешный ½(1 + 1/(1-α)²), — и длина неуспешного пробега после текучки,
// когда в таблице появились надгробия.
//
// Результаты — docs/hashing.md.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "bench_util.hpp"
#include "kvs/chained_table.hpp"
#include "kvs/hash.hpp"
#include "kvs/open_table.hpp"

namespace {

constexpr std::size_t cells = 1u << 20;

std::vector<std::string> keys_of(std::size_t count, std::string_view prefix) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        keys.push_back(std::string(prefix) + std::to_string(i));
    }
    return keys;
}

template <class Table>
void report(const char* name, double load, Table table) {
    const auto count = static_cast<std::size_t>(load * static_cast<double>(cells));
    const auto keys = keys_of(count, "nick:");
    const auto absent = keys_of(count, "ghost:");
    for (const std::string& key : keys) {
        table.insert_or_assign(key, 1);
    }
    // Искать — в другом порядке, чем вставляли. Иначе узлы цепочек,
    // выделенные подряд, читались бы подряд, аппаратная предвыборка
    // угадывала бы следующий, и замер мерил бы её, а не таблицу: в первой
    // версии этого замера цепочки «выигрывали» вдвое именно так.
    std::vector<std::string> lookups = keys;
    std::shuffle(lookups.begin(), lookups.end(), std::mt19937_64(7));
    const auto hit = kvs::bench::measure(3, [] { return 0; }, [&](int) {
        int sum = 0;
        for (const std::string& key : lookups) {
            sum += *table.find(std::string_view(key));
        }
        kvs::bench::keep(sum);
    });
    const auto miss = kvs::bench::measure(3, [] { return 0; }, [&](int) {
        std::size_t found = 0;
        for (const std::string& key : absent) {
            found += table.find(std::string_view(key)) != nullptr ? 1 : 0;
        }
        kvs::bench::keep(found);
    });
    // Средний пробег: через каждый 64-й ключ, этого достаточно для оценки.
    const auto average_probe = [&](const std::vector<std::string>& sample) {
        double total = 0;
        std::size_t taken = 0;
        if constexpr (requires { table.probe_length(std::string_view()); }) {
            for (std::size_t i = 0; i < sample.size(); i += 64, ++taken) {
                total += static_cast<double>(table.probe_length(std::string_view(sample[i])));
            }
        }
        return taken == 0 ? 0.0 : total / static_cast<double>(taken);
    };
    const double probe_hit = average_probe(keys);
    const double probe_miss = average_probe(absent);
    // Текучка: каждый ключ уходит, и приходит новый. Размер постоянен, но
    // в открытой таблице копятся надгробия.
    std::vector<std::string> fresh;
    fresh.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        fresh.push_back("new:" + std::to_string(i));
    }
    const auto churn = kvs::bench::measure(1, [] { return 0; }, [&](int) {
        for (std::size_t i = 0; i < count; ++i) {
            table.erase(std::string_view(keys[i]));
            table.insert_or_assign(fresh[i], 1);
        }
    });
    const double churn_miss = average_probe(absent);
    const double knuth_hit = 0.5 * (1 + 1 / (1 - load));
    const double knuth_miss = 0.5 * (1 + 1 / ((1 - load) * (1 - load)));
    std::cout << std::fixed << "| " << name << " | " << std::setprecision(2) << load << " | "
              << std::setprecision(1) << hit.milliseconds << " | " << miss.milliseconds << " | "
              << churn.milliseconds << " | ";
    if (probe_hit > 0) {
        std::cout << std::setprecision(2) << probe_hit << " / " << probe_miss << " | " << knuth_hit << " / "
                  << knuth_miss << " | " << churn_miss << " |\n";
    } else {
        std::cout << "— | — | — |\n";
    }
    std::cout << std::defaultfloat << std::setprecision(6);
}

}  // namespace

int main() {
    using Chained = kvs::ChainedTable<std::string, int, kvs::Fnv1aHash>;
    using Open = kvs::OpenTable<std::string, int, kvs::Fnv1aHash>;
    std::cout << "2^20 cells, " << "find hit / miss for every key, churn = erase + insert for every key\n\n"
              << "| table | load | hit, ms | miss, ms | churn, ms | probes hit / miss | Knuth hit / miss |"
                 " miss probes after churn |\n"
              << "|---|---|---|---|---|---|---|---|\n";
    for (const double load : {0.5, 0.75, 0.9}) {
        report("chained", load, Chained(kvs::Fnv1aHash{}, 2.0, cells));
        report("open", load, Open(kvs::Fnv1aHash{}, 0.95, cells));
    }
    return 0;
}
