// Этап 8, пункты 2 и 3: распределение хеш-функций на номерах задач.
//
//   ./build-release/taskq_id_histogram
//
// 100 000 номеров в таблицу методом цепочек на 131 072 корзины. Для каждой
// хеш-функции — гистограмма длин цепочек и средняя длина пути успешного
// поиска; рядом — что предсказывает модель случайного хеша: при
// коэффициенте заполнения α число корзин с k ключами распределено по
// Пуассону, n·e^{-α}·α^k / k!.
//
// Два набора номеров: подряд (1, 2, 3, ... — так их выдаёт сервис) и с шагом
// 1024 (так их выдавали бы 1024 генератора, каждый со своим остатком, —
// обычное дело для распределённой очереди). Три функции: FNV-1a,
// тождественная (это std::hash<uint64_t> в libstdc++) и остаток от деления
// на 16. Результаты — docs/hashing.md.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>

#include "taskq/chained_table.hpp"
#include "taskq/hash.hpp"

namespace {

template <class Hash>
void report(const char* title, Hash hash, const std::vector<std::uint64_t>& ids) {
    // Порог огромный: таблица не растёт сама, её размер задан заранее.
    taskq::ChainedTable<std::uint64_t, int, Hash> table(hash, 1e9, 131072);
    for (const std::uint64_t id : ids) {
        table.insert_new(id, 0);
    }
    std::map<std::size_t, std::size_t> histogram;
    std::size_t longest = 0;
    double path = 0;
    for (std::size_t b = 0; b < table.bucket_count(); ++b) {
        const std::size_t length = table.chain_length(b);
        ++histogram[length];
        longest = std::max(longest, length);
        path += static_cast<double>(length * (length + 1)) / 2;
    }
    const double load = table.load_factor();
    std::cout << "\n" << title << ": " << ids.size() << " ids, " << table.bucket_count() << " buckets, load "
              << std::fixed << std::setprecision(3) << load << "\n"
              << "busy buckets " << table.bucket_count() - histogram[0] << ", longest chain " << longest
              << ", average successful search " << std::setprecision(2) << path / static_cast<double>(ids.size())
              << " comparisons\n\n"
              << "| chain length | buckets | Poisson expectation |\n|---|---|---|\n";
    std::size_t shown = 0;
    for (const auto& [length, buckets] : histogram) {
        const double expected = static_cast<double>(table.bucket_count()) * std::exp(-load) *
                                std::pow(load, static_cast<double>(length)) / std::tgamma(length + 1.0);
        std::cout << "| " << length << " | " << buckets << " | " << std::setprecision(0) << expected << " |\n";
        if (++shown == 10) {
            std::cout << "| ... | " << histogram.size() - shown << " more lengths | |\n";
            break;
        }
    }
    std::cout << std::defaultfloat;
}

}  // namespace

int main() {
    std::vector<std::uint64_t> sequential(100000);
    std::ranges::generate(sequential, [next = std::uint64_t{0}]() mutable { return ++next; });
    std::vector<std::uint64_t> strided(100000);
    std::ranges::generate(strided, [next = std::uint64_t{0}]() mutable { return (++next) * 1024; });

    report("FNV-1a, sequential ids", taskq::Fnv1aHash{}, sequential);
    report("identity, sequential ids", taskq::IdentityHash{}, sequential);
    report("id % 16, sequential ids", taskq::ModuloHash{}, sequential);
    report("FNV-1a, ids with stride 1024", taskq::Fnv1aHash{}, strided);
    report("identity, ids with stride 1024", taskq::IdentityHash{}, strided);
    return 0;
}
