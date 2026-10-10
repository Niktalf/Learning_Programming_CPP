#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "imessage.hpp"

using Timestamp = std::chrono::system_clock::time_point;

class Message {
public:
    Message(std::string author, std::unique_ptr<IMessage> body, const Timestamp at = Timestamp{})
        :   author_(std::move(author)), body_(std::move(body)), at_(at) {}

    Message(std::string author, std::string text, const Timestamp at = Timestamp{})
        :   Message(std::move(author), std::make_unique<TextMessage>(std::move(text)), at) {}

    Message(const Message& other) : author_(other.author_), body_(other.body_->clone()), at_(other.at_) {}

    Message& operator=(const Message& other) {
        Message copy(other);
        swap(*this, copy);
        return *this;
    }

    Message(Message&&) noexcept = default;
    Message& operator=(Message&&) noexcept = default;
    ~Message() = default;

    friend void swap(Message& a, Message& b) noexcept {
        a.author_.swap(b.author_);
        a.body_.swap(b.body_);
        std::swap(a.at_, b.at_);
    }

    [[nodiscard]] const std::string& author() const noexcept { return author_; }
    [[nodiscard]] Timestamp at() const noexcept { return at_; }
    [[nodiscard]] std::string_view type_name() const noexcept { return body_->type_name(); }
    [[nodiscard]] std::string to_string() const { return body_->to_string(); }
    [[nodiscard]] std::size_t size() const noexcept { return body_->size(); }

    template <class T> [[nodiscard]] const T* as() const noexcept { return dynamic_cast<const T*>(body_.get()); }
    template <class T> [[nodiscard]] T* as() noexcept { return dynamic_cast<T*>(body_.get()); }

    friend bool operator==(const Message& a, const Message& b) noexcept {
        return a.author_ == b.author_ && a.at_ == b.at_ && a.body_->equals(*b.body_);
    }
    friend bool operator!=(const Message& a, const Message& b) noexcept { return !(a == b); }

private:
    std::string author_;
    std::unique_ptr<IMessage> body_;
    Timestamp at_;
};
