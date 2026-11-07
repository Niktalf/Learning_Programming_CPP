#include "kvs/glob.hpp"

#include <cstddef>

namespace kvs {

// Жадный алгоритм с одной точкой возврата — к последней звёздочке. Работает
// за O(n·m) в худшем случае и без рекурсии: образец «*a*a*a*b» на длинной
// строке не уронит стек.
bool glob_match(std::string_view pattern, std::string_view text) noexcept {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string_view::npos;   // позиция последней '*' в образце
    std::size_t resume = 0;                       // откуда продолжать текст после неё
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            resume = t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

}  // namespace kvs
