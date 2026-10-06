#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class IValue {
public:
    virtual ~IValue() = default;

    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
    [[nodiscard]] virtual std::string to_string() const = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    [[nodiscard]] virtual std::unique_ptr<IValue> clone() const = 0;

    [[nodiscard]] virtual bool equals(const IValue& other) const noexcept = 0;

protected:
    IValue() = default;
    IValue(const IValue&) = default;
    IValue& operator=(const IValue&) = default;
};

class StringValue final : public IValue {
public:
    explicit StringValue(std::string data) noexcept : data_(std::move(data)) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "string"; }
    [[nodiscard]] std::string to_string() const override { return data_; }
    [[nodiscard]] std::size_t size() const noexcept override { return data_.size(); }
    [[nodiscard]] std::unique_ptr<IValue> clone() const override;
    [[nodiscard]] bool equals(const IValue& other) const noexcept override;

    [[nodiscard]] const std::string& data() const noexcept { return data_; }

private:
    std::string data_;
};

class IntValue final : public IValue {
public:
    explicit IntValue(const std::int64_t data) noexcept : data_(data) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "int"; }
    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::size_t size() const noexcept override { return sizeof(data_); }
    [[nodiscard]] std::unique_ptr<IValue> clone() const override;
    [[nodiscard]] bool equals(const IValue& other) const noexcept override;

    [[nodiscard]] std::int64_t data() const noexcept { return data_; }
    void add(const std::int64_t by) noexcept { data_ += by; }

private:
    std::int64_t data_;
};

class FloatValue final : public IValue {
public:
    explicit FloatValue(const double data) noexcept : data_(data) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "float"; }
    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::size_t size() const noexcept override { return sizeof(data_); }
    [[nodiscard]] std::unique_ptr<IValue> clone() const override;
    [[nodiscard]] bool equals(const IValue& other) const noexcept override;

private:
    double data_;
};

class ListValue final : public IValue {
public:
    ListValue() = default;

    [[nodiscard]] std::string_view type_name() const noexcept override { return "list"; }
    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::size_t size() const noexcept override { return items_.size(); }
    [[nodiscard]] std::unique_ptr<IValue> clone() const override;
    [[nodiscard]] bool equals(const IValue& other) const noexcept override;

    void push_back(std::string item) { items_.push_back(std::move(item)); }

private:
    std::vector<std::string> items_;
};
