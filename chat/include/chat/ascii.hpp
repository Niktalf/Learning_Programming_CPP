#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace chat {

// Этап 9: одна точка, где глагол команды приводится к нижнему регистру. До
// этапа 9 это было написано дважды — detail::lower в registry.hpp и
// detail::lower_ascii в static_registry.hpp, — и правило «регистр глагола
// не важен» жило в двух местах (DRY, docs/audit.md, запись 7).
//
// Только ASCII, сознательно: глаголы команд — латиница, а std::tolower
// зависит от локали и для char со знаком — неопределённое поведение на
// отрицательных значениях.
[[nodiscard]] constexpr char lower_ascii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] inline std::string lower_ascii(std::string_view text) {
    std::string result(text.size(), '\0');
    std::ranges::transform(text, result.begin(), [](char c) { return lower_ascii(c); });
    return result;
}

// Равны ли слова без учёта регистра — constexpr, для проверок при компиляции.
[[nodiscard]] constexpr bool same_verb(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, {}, [](char c) { return lower_ascii(c); },
                              [](char c) { return lower_ascii(c); });
}

}  // namespace chat
