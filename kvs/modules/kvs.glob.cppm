// Домашнее задание этапа 5: модуль. Сопоставление с образцом KEYS вынесено
// в именованный модуль kvs.glob. Что получилось и что нет — docs/modules.md.
//
// Заголовки стандартной библиотеки подключаются в глобальном фрагменте
// модуля: import std потребовал бы CMake 3.30 с экспериментальным флагом,
// а проект собирается CMake 3.28.

module;

#include <cstddef>
#include <string_view>

export module kvs.glob;

export namespace kvs::modules {

// Тот же алгоритм, что в src/glob.cpp: '*' — любая последовательность,
// '?' — один символ. Определение — прямо в интерфейсе модуля: отдельной
// единицы реализации для одной функции не нужно, а инлайнить её через
// границу модуля компилятор может и так.
bool glob_match(std::string_view pattern, std::string_view text) noexcept {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string_view::npos;
    std::size_t resume = 0;
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

}  // namespace kvs::modules
