#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

struct SetCommand    { std::string_view key; std::string_view value; };
struct GetCommand    { std::string_view key; };
struct DelCommand    { std::string_view key; };
struct ExistsCommand { std::string_view key; };
struct TypeCommand   { std::string_view key; };
struct IncrCommand   { std::string_view key; std::int64_t by; };

using Command = std::variant<SetCommand, GetCommand, DelCommand, ExistsCommand, TypeCommand, IncrCommand>;
