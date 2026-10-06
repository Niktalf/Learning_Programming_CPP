#pragma once

#include <string>
#include <utility>

class User {
public:
    explicit User(std::string nick) noexcept : nick_(std::move(nick)) {}

    [[nodiscard]] const std::string& nick() const noexcept { return nick_; }

private:
    std::string nick_;
};
