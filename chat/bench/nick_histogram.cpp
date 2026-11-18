// Этап 8, пункты 2 и 3: распределение хеш-функций на никах.
//
//   ./build-release/chat_nick_histogram
//
// 100 000 ников в таблицу методом цепочек — с FNV-1a и с суммой кодов
// символов. Для каждой — гистограмма длин цепочек и средняя длина пути
// успешного поиска; рядом — что предсказывает модель случайного хеша:
// при коэффициенте заполнения α число корзин с k ключами распределено по
// Пуассону, n·e^{-α}·α^k / k!.
//
// Три набора ников: случайные строчные длиной 6–10, случайные строго длины
// 8 (ники одной длины — худший случай суммы кодов, кроме анаграмм) и
// анаграммы — перестановки одного ника. Результаты — docs/hashing.md.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "chat/chained_table.hpp"
#include "chat/hash.hpp"
#include "nick_names.hpp"

namespace {

template <class Hash>
void report(const char* title, Hash hash, const std::vector<std::string>& nicks) {
    // Порог огромный: таблица не растёт сама, её размер задан заранее —
    // 131 072 корзины, ближайшая степень двойки к числу ников.
    chat::ChainedTable<std::string, int, Hash> table(hash, 1e9, 131072);
    for (const std::string& nick : nicks) {
        table.insert_new(nick, 0);
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
    std::cout << "\n" << title << ": " << nicks.size() << " nicks, " << table.bucket_count()
              << " buckets, load " << std::fixed << std::setprecision(3) << load << "\n"
              << "busy buckets " << table.bucket_count() - histogram[0] << ", longest chain " << longest
              << ", average successful search " << std::setprecision(2)
              << path / static_cast<double>(nicks.size()) << " comparisons\n\n"
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
    const auto mixed = chat::bench::random_nicks(100000, 8);
    report("FNV-1a, random nicks of length 6-10", chat::Fnv1aHash{}, mixed);
    report("char sum, random nicks of length 6-10", chat::CharSumHash{}, mixed);

    const auto fixed = chat::bench::random_nicks(100000, 8, 8, 8);
    report("FNV-1a, random nicks of length 8", chat::Fnv1aHash{}, fixed);
    report("char sum, random nicks of length 8", chat::CharSumHash{}, fixed);

    // Анаграммы: все перестановки одного ника — у суммы кодов один хеш.
    std::vector<std::string> anagrams;
    std::string letters = "abcdefgh";
    do {
        anagrams.push_back(letters);
    } while (std::ranges::next_permutation(letters).found && anagrams.size() < 40000);
    report("FNV-1a, anagrams", chat::Fnv1aHash{}, anagrams);
    report("char sum, anagrams", chat::CharSumHash{}, anagrams);
    return 0;
}
