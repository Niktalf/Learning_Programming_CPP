// Звёздочка этапа 8: цепочки против открытой адресации на реестре ников при
// коэффициенте заполнения 0.5, 0.75 и 0.9.
//
//   ./build-release/chat_bench_open
//
// Таблица создаётся сразу нужного размера — 2^20 ячеек — с порогом выше
// заданного α, чтобы она не росла сама, и заполняется ровно до α. Затем:
// поиск каждого ника в случайном порядке, поиск стольких же отсутствующих и
// «текучка» — каждый пользователь выходит, и входит новый. Рядом —
// измеренная средняя длина пробега, оценка Кнута для линейного
// пробирования (успешный поиск ½(1 + 1/(1-α)), неуспешный ½(1 + 1/(1-α)²)),
// число надгробий и длина неуспешного пробега после текучки.
//
// Результаты — docs/hashing.md.

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "bench_util.hpp"
#include "chat/chained_table.hpp"
#include "chat/hash.hpp"
#include "chat/open_table.hpp"
#include "nick_names.hpp"

namespace {

constexpr std::size_t cells = 1u << 20;

template <class Table>
void report(const char* name, double load, Table table) {
    const auto count = static_cast<std::size_t>(load * static_cast<double>(cells));
    // Три непересекающихся набора из одного распределения: основной,
    // отсутствующие и новые для текучки. Распределение длин у всех одно —
    // иначе отсутствующие ники с плохим хешем попадали бы в пустые корзины
    // (сумма кодов зависит от длины), и промах выглядел бы дешёвым.
    const auto all = chat::bench::random_nicks(3 * count, 1);
    const std::vector<std::string> nicks(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(count));
    const std::vector<std::string> absent(all.begin() + static_cast<std::ptrdiff_t>(count),
                                          all.begin() + static_cast<std::ptrdiff_t>(2 * count));
    const std::vector<std::string> fresh(all.begin() + static_cast<std::ptrdiff_t>(2 * count), all.end());
    for (const std::string& nick : nicks) {
        table.insert_new(nick, 1);
    }
    // Искать — в другом порядке, чем вставляли: иначе узлы цепочек,
    // выделенные подряд, читались бы подряд, и замер мерил бы аппаратную
    // предвыборку, а не таблицу.
    std::vector<std::string> lookups = nicks;
    std::shuffle(lookups.begin(), lookups.end(), std::mt19937(7));
    const auto hit = chat::bench::measure(3, [] { return 0; }, [&](int) {
        int sum = 0;
        for (const std::string& nick : lookups) {
            sum += *table.find(std::string_view(nick));
        }
        chat::bench::keep(sum);
    });
    const auto miss = chat::bench::measure(3, [] { return 0; }, [&](int) {
        std::size_t found = 0;
        for (const std::string& nick : absent) {
            found += table.find(std::string_view(nick)) != nullptr ? 1 : 0;
        }
        chat::bench::keep(found);
    });
    // Средний пробег — по каждому 64-му нику, этого достаточно для оценки.
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
    const double probe_hit = average_probe(nicks);
    const double probe_miss = average_probe(absent);
    const auto churn = chat::bench::measure(1, [] { return 0; }, [&](int) {
        for (std::size_t i = 0; i < count; ++i) {
            table.erase(std::string_view(nicks[i]));
            table.insert_new(fresh[i], 1);
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
    using Chained = chat::ChainedTable<std::string, int, chat::Fnv1aHash>;
    using Open = chat::OpenTable<std::string, int, chat::Fnv1aHash>;
    std::cout << "2^20 cells; find hit / miss for every nick; churn = logout + login for every nick\n\n"
              << "| table | load | hit, ms | miss, ms | churn, ms | probes hit / miss | Knuth hit / miss |"
                 " miss probes after churn |\n"
              << "|---|---|---|---|---|---|---|---|\n";
    for (const double load : {0.5, 0.75, 0.9}) {
        report("chained", load, Chained(chat::Fnv1aHash{}, 2.0, cells));
        report("open", load, Open(chat::Fnv1aHash{}, 0.95, cells));
    }
    return 0;
}
