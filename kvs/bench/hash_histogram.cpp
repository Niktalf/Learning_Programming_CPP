// Этап 8, пункты 2 и 3: распределение хеш-функций по корзинам.
//
//   ./build-release/kvs_hash_histogram
//
// 100 000 ключей в таблицу методом цепочек — сначала с FNV-1a, потом с
// суммой кодов символов. Для каждой — гистограмма длин цепочек: сколько
// корзин пусты, сколько содержат один ключ, два, и так далее, — и средняя
// длина пути успешного поиска. Для FNV-1a рядом — то, что предсказывает
// модель случайного хеша: при коэффициенте заполнения α число корзин с k
// ключами распределено по Пуассону, e^{-α} α^k / k!.
//
// Ключи — двух видов: «user:N» с N подряд — так выглядят настоящие ключи,
// и именно на них плохая функция проваливается, — и анаграммы.
// Результаты — docs/hashing.md.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "kvs/chained_table.hpp"
#include "kvs/hash.hpp"

namespace {

constexpr std::size_t key_count = 100000;

template <class Hash>
void report(const char* title, Hash hash, const std::vector<std::string>& keys) {
    // Порог огромный: таблица не растёт сама, её размер задан заранее —
    // 131 072 корзины, ближайшая степень двойки к числу ключей.
    kvs::ChainedTable<std::string, int, Hash> table(hash, 1e9, 131072);
    for (const std::string& key : keys) {
        table.insert_or_assign(key, 0);
    }
    std::map<std::size_t, std::size_t> histogram;
    std::size_t longest = 0;
    double path = 0;   // сумма по ключам: сколько сравнений до него в цепочке
    for (std::size_t b = 0; b < table.bucket_count(); ++b) {
        const std::size_t length = table.chain_length(b);
        ++histogram[length];
        longest = std::max(longest, length);
        path += static_cast<double>(length * (length + 1)) / 2;
    }
    const double load = table.load_factor();
    std::cout << "\n" << title << ": " << keys.size() << " keys, " << table.bucket_count()
              << " buckets, load " << std::fixed << std::setprecision(3) << load << "\n"
              << "longest chain " << longest << ", average successful search "
              << std::setprecision(2) << path / static_cast<double>(keys.size()) << " comparisons\n\n"
              << "| chain length | buckets | Poisson expectation |\n|---|---|---|\n";
    std::size_t shown = 0;
    for (const auto& [length, buckets] : histogram) {
        const double expected = static_cast<double>(table.bucket_count()) * std::exp(-load) *
                                std::pow(load, static_cast<double>(length)) / std::tgamma(length + 1.0);
        std::cout << "| " << length << " | " << buckets << " | " << std::setprecision(0) << expected << " |\n";
        if (++shown == 12) {
            std::cout << "| ... | " << histogram.size() - shown << " more lengths | |\n";
            break;
        }
    }
}

}  // namespace

int main() {
    std::vector<std::string> sequential;
    sequential.reserve(key_count);
    for (std::size_t i = 0; i < key_count; ++i) {
        sequential.push_back("user:" + std::to_string(i));
    }
    report("FNV-1a, user:N", kvs::Fnv1aHash{}, sequential);
    report("char sum, user:N", kvs::CharSumHash{}, sequential);

    // Анаграммы: все перестановки одной строки — у суммы кодов один хеш.
    std::vector<std::string> anagrams;
    std::string letters = "abcdefgh";
    do {
        anagrams.push_back(letters);
    } while (std::ranges::next_permutation(letters).found && anagrams.size() < 40000);
    report("FNV-1a, anagrams", kvs::Fnv1aHash{}, anagrams);
    report("char sum, anagrams", kvs::CharSumHash{}, anagrams);
    return 0;
}
