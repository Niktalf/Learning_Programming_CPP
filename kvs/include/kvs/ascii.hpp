#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace kvs {

// Этап 9: одна точка, где глагол команды приводится к верхнему регистру. До
// этапа 9 это было написано дважды — CommandRegistry::upper и
// detail::upper_ascii в static_registry.hpp, — и правило «регистр глагола
// не важен» жило в двух местах (DRY, docs/audit.md, запись 6).
//
// Только ASCII, сознательно: глаголы команд — латиница, а std::toupper
// зависит от локали и для char со знаком — неопределённое поведение на
// отрицательных значениях.
[[nodiscard]] constexpr char upper_ascii(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] inline std::string upper_ascii(std::string_view text) {
    std::string result(text.size(), '\0');
    std::ranges::transform(text, result.begin(), [](char c) { return upper_ascii(c); });
    return result;
}

// Равны ли слова без учёта регистра — constexpr, для проверок при компиляции.
[[nodiscard]] constexpr bool same_verb(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, {}, [](char c) { return upper_ascii(c); },
                              [](char c) { return upper_ascii(c); });
}

}  // namespace kvs
