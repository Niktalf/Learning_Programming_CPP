#pragma once

#include <string>
#include <utility>

class Worker {
public:
    explicit Worker(std::string name) noexcept : name_(std::move(name)) {}

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
};
