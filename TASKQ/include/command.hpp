#pragma once

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

struct CreateCommand  { std::string_view queue; };
struct SubmitCommand  { std::string_view queue; int priority; std::string_view body; };
struct BatchCommand   { std::vector<std::string_view> queues; int priority; std::string_view body; };
struct PollCommand    { std::string_view queue; };
struct AckCommand     { std::uint64_t id; };
struct FailCommand    { std::uint64_t id; };
struct ReleaseCommand { std::uint64_t id; };
struct StatusCommand  { std::uint64_t id; };
struct QueuesCommand  {};
struct TypeCommand    { std::uint64_t id; };
struct BumpCommand    { std::uint64_t id; std::int64_t by; };

using Command = std::variant<CreateCommand, SubmitCommand, BatchCommand, PollCommand, AckCommand, FailCommand,
      ReleaseCommand, StatusCommand, QueuesCommand, TypeCommand, BumpCommand>;
