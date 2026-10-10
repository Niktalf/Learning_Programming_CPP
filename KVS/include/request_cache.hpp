#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>

#include "command.hpp"
#include "parser.hpp"

class Request {
public:
    explicit Request(std::string text) : line_(std::move(text)), command_(parse(line_)) {}

    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;

    [[nodiscard]] const Command& command() const noexcept { return command_; }
    [[nodiscard]] std::string_view text() const noexcept { return line_.view(); }

private:
    InputLine line_;
    Command command_;
};

class RequestCache {
public:
    [[nodiscard]] std::shared_ptr<const Request> get(const std::string& text);

    std::size_t purge();

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::map<std::string, std::weak_ptr<const Request>> entries_;
};
