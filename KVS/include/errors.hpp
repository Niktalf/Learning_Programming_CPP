#pragma once

#include <stdexcept>
#include <string>

class KvError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ParseError : public KvError {
public:
    ParseError(std::string reason, std::string input);

    [[nodiscard]] const std::string& reason() const noexcept { return reason_; }
    [[nodiscard]] const std::string& input() const noexcept { return input_; }

private:
    std::string reason_;
    std::string input_;
};

class KeyNotFoundError : public KvError {
public:
    explicit KeyNotFoundError(std::string key);

    [[nodiscard]] const std::string& key() const noexcept { return key_; }

private:
    std::string key_;
};

class TypeMismatchError : public KvError {
public:
    TypeMismatchError(std::string key, std::string expected, std::string actual);

    [[nodiscard]] const std::string& key() const noexcept { return key_; }
    [[nodiscard]] const std::string& expected() const noexcept { return expected_; }
    [[nodiscard]] const std::string& actual() const noexcept { return actual_; }

private:
    std::string key_;
    std::string expected_;
    std::string actual_;
};
