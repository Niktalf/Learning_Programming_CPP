// Звёздочка этапа 8: цепочки против открытой адресации на индексе по номеру
// при коэффициенте заполнения 0.5, 0.75 и 0.9.
//
//   ./build-release/taskq_bench_open
//
// Таблица создаётся сразу нужного размера — 2^20 ячеек — с порогом выше
// заданного α и заполняется ровно до α номерами подряд. Затем: поиск каждого
// номера в случайном порядке, поиск стольких же отсутствующих и «текучка»,
// как в очереди: выдать старейшую задачу, поставить новую. Рядом —
// измеренная средняя длина пробега, оценка Кнута для линейного
// пробирования (успешный поиск ½(1 + 1/(1-α)), неуспешный ½(1 + 1/(1-α)²))
// и длина неуспешного пробега после текучки.
//
// Значение в ячейке — 32 байта, как запись индекса хранилища (очередь,
// позиция). Результаты — docs/hashing.md.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

#include "bench_util.hpp"
#include "taskq/chained_table.hpp"
#include "taskq/hash.hpp"
#include "taskq/open_table.hpp"

namespace {

constexpr std::size_t cells = 1u << 20;

struct Record {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    std::uint64_t c = 0;
    std::uint64_t d = 0;
};

template <class Table>
void report(const char* name, double load, Table table) {
    const auto count = static_cast<std::size_t>(load * static_cast<double>(cells));
    for (std::uint64_t id = 1; id <= count; ++id) {
        table.insert_new(id, Record{id, 0, 0, 0});
    }
    std::vector<std::uint64_t> lookups(count);
    std::ranges::generate(lookups, [next = std::uint64_t{0}]() mutable { return ++next; });
    std::ranges::shuffle(lookups, std::mt19937(7));
    const auto hit = taskq::bench::measure(3, [] { return 0; }, [&](int) {
        std::uint64_t sum = 0;
        for (const std::uint64_t id : lookups) {
            sum += table.find(id)->a;
        }
        taskq::bench::keep(sum);
    });
    const auto miss = taskq::bench::measure(3, [] { return 0; }, [&](int) {
        std::size_t found = 0;
        for (const std::uint64_t id : lookups) {
            found += table.find(id + 2 * count) != nullptr ? 1 : 0;
        }
        taskq::bench::keep(found);
    });
    // Средний пробег по каждому 64-му номеру; offset сдвигает номера в
    // заведомо отсутствующие.
    const auto average_probe = [&](std::uint64_t offset) {
        double total = 0;
        std::size_t taken = 0;
        if constexpr (requires { table.probe_length(std::uint64_t{}); }) {
            for (std::size_t i = 0; i < count; i += 64, ++taken) {
                total += static_cast<double>(table.probe_length(lookups[i] + offset));
            }
        }
        return taken == 0 ? 0.0 : total / static_cast<double>(taken);
    };
    const double probe_hit = average_probe(0);
    const double probe_miss = average_probe(2 * count);
    // Текучка: окно номеров сдвигается на count — каждая выдача оставляет
    // надгробие (или пустую ячейку у цепочек).
    const auto churn = taskq::bench::measure(1, [] { return 0; }, [&](int) {
        for (std::uint64_t id = 1; id <= count; ++id) {
            table.erase(id);
            table.insert_new(id + count, Record{});
        }
    });
    const double churn_miss = average_probe(3 * count);
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
    using Chained = taskq::ChainedTable<std::uint64_t, Record, taskq::Fnv1aHash>;
    using Open = taskq::OpenTable<std::uint64_t, Record, taskq::Fnv1aHash>;
    std::cout << "2^20 cells, sequential ids; find hit / miss for every id; churn = poll oldest + submit new\n\n"
              << "| table | load | hit, ms | miss, ms | churn, ms | probes hit / miss | Knuth hit / miss |"
                 " miss probes after churn |\n"
              << "|---|---|---|---|---|---|---|---|\n";
    for (const double load : {0.5, 0.75, 0.9}) {
        report("chained", load, Chained(taskq::Fnv1aHash{}, 2.0, cells));
        report("open", load, Open(taskq::Fnv1aHash{}, 0.95, cells));
    }
    return 0;
}
