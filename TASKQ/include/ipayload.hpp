#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

class IPayload {
public:
    virtual ~IPayload() = default;

    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
    [[nodiscard]] virtual std::string to_string() const = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    [[nodiscard]] virtual std::unique_ptr<IPayload> clone() const = 0;
    [[nodiscard]] virtual bool equals(const IPayload& other) const noexcept = 0;

protected:
    IPayload() = default;
    IPayload(const IPayload&) = default;
    IPayload& operator=(const IPayload&) = default;
};

class TextPayload final : public IPayload {
public:
    explicit TextPayload(std::string command) noexcept : command_(std::move(command)) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "text"; }
    [[nodiscard]] std::string to_string() const override { return command_; }
    [[nodiscard]] std::size_t size() const noexcept override { return command_.size(); }
    [[nodiscard]] std::unique_ptr<IPayload> clone() const override;
    [[nodiscard]] bool equals(const IPayload& other) const noexcept override;

private:
    std::string command_;
};

class NumericPayload final : public IPayload {
public:
    explicit NumericPayload(const std::int64_t value) noexcept : value_(value) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "number"; }
    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::size_t size() const noexcept override { return sizeof(value_); }
    [[nodiscard]] std::unique_ptr<IPayload> clone() const override;
    [[nodiscard]] bool equals(const IPayload& other) const noexcept override;

    [[nodiscard]] std::int64_t value() const noexcept { return value_; }
    void add(const std::int64_t by) noexcept { value_ += by; }

private:
    std::int64_t value_;
};

class BinaryPayload final : public IPayload {
public:
    explicit BinaryPayload(std::string bytes) noexcept : bytes_(std::move(bytes)) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "binary"; }
    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::size_t size() const noexcept override { return bytes_.size(); }
    [[nodiscard]] std::unique_ptr<IPayload> clone() const override;
    [[nodiscard]] bool equals(const IPayload& other) const noexcept override;

private:
    std::string bytes_;
};
