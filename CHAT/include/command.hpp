#pragma once

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

struct JoinCommand    { std::string_view room; };
struct LeaveCommand   { std::string_view room; };
struct MsgCommand     { std::vector<std::string_view> rooms; std::string_view text; };
struct HistoryCommand { std::string_view room; std::size_t limit; };
struct RoomsCommand   {};
struct TypeCommand    { std::string_view room; std::size_t index; };
struct ReactCommand   { std::string_view room; std::size_t index; std::int64_t by; };
struct NickCommand    { std::string_view nick; };
struct LogoutCommand  { std::string_view nick; };
struct InviteCommand  { std::string_view room; std::string_view nick; };
struct WhoCommand     { std::string_view room; };

using Command = std::variant<JoinCommand, LeaveCommand, MsgCommand, HistoryCommand, RoomsCommand, TypeCommand,
                             ReactCommand, NickCommand, LogoutCommand, InviteCommand, WhoCommand>;
