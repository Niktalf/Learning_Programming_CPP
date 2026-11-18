#pragma once

#include <cstddef>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace chat::bench {

// Ники для замеров этапа 8: count различных случайных ников из строчных
// латинских букв длиной от 6 до 10 — так выглядят настоящие ники, — с
// фиксированной затравкой генератора, чтобы прогоны были воспроизводимы.
// Повторы отбрасываются (std::unordered_set здесь — вспомогательный, не
// замеряемый).
inline std::vector<std::string> random_nicks(std::size_t count, unsigned seed, std::size_t min_length = 6,
                                             std::size_t max_length = 10) {
    std::mt19937 generator(seed);
    std::uniform_int_distribution<int> letter('a', 'z');
    std::uniform_int_distribution<std::size_t> length(min_length, max_length);
    std::unordered_set<std::string> seen;
    std::vector<std::string> nicks;
    nicks.reserve(count);
    while (nicks.size() < count) {
        std::string nick(length(generator), ' ');
        for (char& c : nick) {
            c = static_cast<char>(letter(generator));
        }
        if (seen.insert(nick).second) {
            nicks.push_back(std::move(nick));
        }
    }
    return nicks;
}

}  // namespace chat::bench
