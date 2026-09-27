#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "command.hpp"

class InputLine {
public:
    explicit InputLine(std::string text) noexcept : text_(std::move(text)) {}

    InputLine(const InputLine&) = delete;
    InputLine& operator=(const InputLine&) = delete;
    InputLine(InputLine&&) = delete;
    InputLine& operator=(InputLine&&) = delete;

    [[nodiscard]] std::string_view view() const noexcept { return text_; }

private:
    std::string text_;
};

[[nodiscard]] std::optional<Command> parse(const InputLine& line);

std::optional<Command> parse(InputLine&&) = delete;
