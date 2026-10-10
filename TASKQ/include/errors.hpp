#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include "state.hpp"

class TaskQError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ParseError : public TaskQError {
public:
    ParseError(std::string reason, std::string input);

    [[nodiscard]] const std::string& reason() const noexcept { return reason_; }
    [[nodiscard]] const std::string& input() const noexcept { return input_; }

private:
    std::string reason_;
    std::string input_;
};

class UnknownQueueError : public TaskQError {
public:
    explicit UnknownQueueError(std::string queue);

    [[nodiscard]] const std::string& queue() const noexcept { return queue_; }

private:
    std::string queue_;
};

class UnknownTaskError : public TaskQError {
public:
    explicit UnknownTaskError(std::uint64_t id);

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }

private:
    std::uint64_t id_;
};

class UnknownWorkerError : public TaskQError {
public:
    explicit UnknownWorkerError(std::string name);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
};

class InvalidTransitionError : public TaskQError {
public:
    InvalidTransitionError(std::uint64_t id, State from, State to);

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] State from() const noexcept { return from_; }
    [[nodiscard]] State to() const noexcept { return to_; }

private:
    std::uint64_t id_;
    State from_;
    State to_;
};

class TypeMismatchError : public TaskQError {
public:
    TypeMismatchError(std::uint64_t id, std::string expected, std::string actual);

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] const std::string& expected() const noexcept { return expected_; }
    [[nodiscard]] const std::string& actual() const noexcept { return actual_; }

private:
    std::uint64_t id_;
    std::string expected_;
    std::string actual_;
};
