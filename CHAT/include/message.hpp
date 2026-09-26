#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

using Timestamp = std::chrono::system_clock::time_point;

class Message {
    public:
    Message() = default;

    Message(std::string author, std::string text, Timestamp at = Timestamp{})
        : author_(std::move(author)), text_(std::move(text)), at_(at) {}

    const std::string& author() const noexcept { return author_; }
    const std::string& text() const noexcept { return text_; }
    Timestamp at() const noexcept { return at_; }
    std::size_t size() const noexcept { return text_.size(); }

private:
    std::string author_;
    std::string text_;
    Timestamp at_ = Timestamp{};
};

inline bool operator==(const Message& lhs, const Message& rhs) noexcept {
    return lhs.author() == rhs.author() && lhs.text() == rhs.text() &&
        lhs.at() == rhs.at();
}

inline bool operator!=(const Message& lhs, const Message& rhs) noexcept {
    return !(lhs == rhs);
}
