#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using Tokens = std::vector<std::string_view>;

[[nodiscard]] Tokens tokenize(std::string_view line, std::size_t max_tokens = (std::numeric_limits<std::size_t>::max)());
Tokens tokenize(std::string&&, std::size_t = 0) = delete;
