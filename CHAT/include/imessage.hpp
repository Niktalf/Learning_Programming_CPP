#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

class IMessage {
public:
    virtual ~IMessage() = default;

    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
    [[nodiscard]] virtual std::string to_string() const = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    [[nodiscard]] virtual std::unique_ptr<IMessage> clone() const = 0;
    [[nodiscard]] virtual bool equals(const IMessage& other) const noexcept = 0;

protected:
    IMessage() = default;
    IMessage(const IMessage&) = default;
    IMessage& operator=(const IMessage&) = default;
};

class TextMessage final : public IMessage {
public:
    explicit TextMessage(std::string text) noexcept : text_(std::move(text)) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "text"; }
    [[nodiscard]] std::string to_string() const override { return text_; }
    [[nodiscard]] std::size_t size() const noexcept override { return text_.size(); }
    [[nodiscard]] std::unique_ptr<IMessage> clone() const override;
    [[nodiscard]] bool equals(const IMessage& other) const noexcept override;

private:
    std::string text_;
};

class SystemMessage final : public IMessage {
public:
    explicit SystemMessage(std::string text) noexcept : text_(std::move(text)) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "notice"; }
    [[nodiscard]] std::string to_string() const override { return "* " + text_; }
    [[nodiscard]] std::size_t size() const noexcept override { return text_.size(); }
    [[nodiscard]] std::unique_ptr<IMessage> clone() const override;
    [[nodiscard]] bool equals(const IMessage& other) const noexcept override;

private:
    std::string text_;
};

class ReactionMessage final : public IMessage {
public:
    ReactionMessage(std::string emoji, const std::int64_t count) noexcept : emoji_(std::move(emoji)), count_(count) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "reaction"; }
    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::size_t size() const noexcept override { return emoji_.size(); }
    [[nodiscard]] std::unique_ptr<IMessage> clone() const override;
    [[nodiscard]] bool equals(const IMessage& other) const noexcept override;

    [[nodiscard]] std::int64_t count() const noexcept { return count_; }
    void add(const std::int64_t by) noexcept { count_ += by; }

private:
    std::string emoji_;
    std::int64_t count_;
};
