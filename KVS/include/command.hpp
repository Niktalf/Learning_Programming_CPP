#pragma once

#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

struct SetCommand    { std::string_view key; std::string_view value; };
struct GetCommand    { std::string_view key; };
struct DelCommand    { std::string_view key; };
struct ExistsCommand { std::string_view key; };
struct TypeCommand   { std::string_view key; };
struct IncrCommand   { std::string_view key; std::int64_t by; };
struct MsetCommand   { std::vector<std::pair<std::string_view, std::string_view>> pairs; };

struct RPushCommand  { std::string_view key; std::string_view item; };
struct LLenCommand   { std::string_view key; };

using Command = std::variant<SetCommand, GetCommand, DelCommand, ExistsCommand, TypeCommand, IncrCommand,
                             MsetCommand, RPushCommand, LLenCommand>;
