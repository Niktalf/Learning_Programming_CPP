#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

using Timestamp = std::chrono::system_clock::time_point;

struct TextBody     { std::string text; };
struct SystemNotice { std::string text; };
struct Reaction     { std::string emoji; std::int64_t count = 0; };

inline bool operator==(const TextBody& a, const TextBody& b) { return a.text == b.text; }
inline bool operator==(const SystemNotice& a, const SystemNotice& b) { return a.text == b.text; }
inline bool operator==(const Reaction& a, const Reaction& b) { return a.emoji == b.emoji && a.count == b.count; }

using Body = std::variant<TextBody, SystemNotice, Reaction>;

class Message {
public:
    Message() = default;

    Message(std::string author, Body body, const Timestamp at = Timestamp{})
        : author_(std::move(author)), body_(std::move(body)), at_(at) {}

    Message(std::string author, std::string text, const Timestamp at = Timestamp{})
        : Message(std::move(author), Body{TextBody{std::move(text)}}, at) {}

    [[nodiscard]] const std::string& author() const noexcept { return author_; }
    [[nodiscard]] const Body& body() const noexcept { return body_; }
    [[nodiscard]] Timestamp at() const noexcept { return at_; }

    [[nodiscard]] std::string_view type_name() const noexcept;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::size_t size() const noexcept;

    template <class T> [[nodiscard]] const T* get_if() const noexcept { return std::get_if<T>(&body_); }
    template <class T> [[nodiscard]] T* get_if() noexcept { return std::get_if<T>(&body_); }
    template <class T> [[nodiscard]] bool holds() const noexcept { return std::holds_alternative<T>(body_); }

private:
    std::string author_;
    Body body_;
    Timestamp at_ = Timestamp{};
};

inline bool operator==(const Message& lhs, const Message& rhs) {
    return lhs.author() == rhs.author() && lhs.body() == rhs.body() && lhs.at() == rhs.at();
}

inline bool operator!=(const Message& lhs, const Message& rhs) { return !(lhs == rhs); }
