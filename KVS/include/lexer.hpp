#pragma once

#include <string>
#include <string_view>
#include <vector>

using Tokens = std::vector<std::string_view>;

[[nodiscard]] Tokens tokenize(std::string_view line);
Tokens tokenize(std::string&&) = delete;
