#pragma once

#include <string_view>

namespace kvs {

// Сопоставление с образцом, как у KEYS в Redis: '*' — любая
// последовательность символов, '?' — один любой символ. Без классов
// символов в скобках: для учебной команды этого достаточно.
[[nodiscard]] bool glob_match(std::string_view pattern, std::string_view text) noexcept;

}  // namespace kvs
