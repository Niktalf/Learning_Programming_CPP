#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

struct SubmitCommand { std::string_view queue; int priority; std::string_view body; };
struct PollCommand   { std::string_view queue; };
struct AckCommand    { std::string_view queue; };
struct StatusCommand { std::string_view queue; };
struct QueuesCommand {};
struct TypeCommand   { std::string_view queue; };
struct BumpCommand   { std::string_view queue; std::int64_t by; };

using Command = std::variant<SubmitCommand, PollCommand, AckCommand, StatusCommand, QueuesCommand, TypeCommand, BumpCommand>;
