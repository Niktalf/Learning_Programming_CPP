#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

struct JoinCommand    { std::string_view room; };
struct LeaveCommand   { std::string_view room; };
struct MsgCommand     { std::string_view room; std::string_view text; };
struct HistoryCommand { std::string_view room; std::size_t limit; };
struct RoomsCommand   {};
struct TypeCommand    { std::string_view room; std::size_t index; };
struct ReactCommand   { std::string_view room; std::size_t index; std::int64_t by; };

using Command = std::variant<JoinCommand, LeaveCommand, MsgCommand, HistoryCommand, RoomsCommand, TypeCommand, ReactCommand>;
