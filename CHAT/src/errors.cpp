#include "errors.hpp"

#include <utility>

ParseError::ParseError(std::string reason, std::string input)
    :   ChatError("parse error: " + reason + " in \"" + input + "\""),
        reason_(std::move(reason)), input_(std::move(input)) {}

UnknownRoomError::UnknownRoomError(std::string room)
    :   ChatError("no such room: " + room), room_(std::move(room)) {}

NicknameTakenError::NicknameTakenError(std::string nick)
    :   ChatError("nickname already in use: " + nick), nick_(std::move(nick)) {}

TypeMismatchError::TypeMismatchError(std::string room, std::string expected, std::string actual)
    :   ChatError("type mismatch in " + room + ": expected " + expected + ", got " + actual),
        room_(std::move(room)), expected_(std::move(expected)), actual_(std::move(actual)) {}
