#include "errors.hpp"

#include <string>
#include <utility>

ParseError::ParseError(std::string reason, std::string input)
    :   TaskQError("parse error: " + reason + " in \"" + input + "\""),
        reason_(std::move(reason)), input_(std::move(input)) {}

UnknownQueueError::UnknownQueueError(std::string queue)
    :   TaskQError("no such queue: " + queue), queue_(std::move(queue)) {}

UnknownTaskError::UnknownTaskError(const std::uint64_t id)
    :   TaskQError("no such task: " + std::to_string(id)), id_(id) {}

InvalidTransitionError::InvalidTransitionError(const std::uint64_t id, const State from, const State to)
    :   TaskQError("task " + std::to_string(id) + ": " + std::string(name(from)) + " -> " +
        std::string(name(to)) + " not allowed"), id_(id), from_(from), to_(to) {}

TypeMismatchError::TypeMismatchError(const std::uint64_t id, std::string expected, std::string actual)
    :   TaskQError("task " + std::to_string(id) + ": expected " + expected + ", got " + actual),
        id_(id), expected_(std::move(expected)), actual_(std::move(actual)) {}
