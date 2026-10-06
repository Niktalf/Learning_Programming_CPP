#pragma once

#include <stdexcept>
#include <string>

class ChatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ParseError : public ChatError {
public:
    ParseError(std::string reason, std::string input);

    [[nodiscard]] const std::string& reason() const noexcept { return reason_; }
    [[nodiscard]] const std::string& input() const noexcept { return input_; }

private:
    std::string reason_;
    std::string input_;
};

class UnknownRoomError : public ChatError {
public:
    explicit UnknownRoomError(std::string room);

    [[nodiscard]] const std::string& room() const noexcept { return room_; }

private:
    std::string room_;
};

class NicknameTakenError : public ChatError {
public:
    explicit NicknameTakenError(std::string nick);

    [[nodiscard]] const std::string& nick() const noexcept { return nick_; }

private:
    std::string nick_;
};

class UnknownUserError : public ChatError {
public:
    explicit UnknownUserError(std::string nick);

    [[nodiscard]] const std::string& nick() const noexcept { return nick_; }

private:
    std::string nick_;
};

class TypeMismatchError : public ChatError {
public:
    TypeMismatchError(std::string room, std::string expected, std::string actual);

    [[nodiscard]] const std::string& room() const noexcept { return room_; }
    [[nodiscard]] const std::string& expected() const noexcept { return expected_; }
    [[nodiscard]] const std::string& actual() const noexcept { return actual_; }

private:
    std::string room_;
    std::string expected_;
    std::string actual_;
};
