#include "errors.hpp"

#include <utility>

ParseError::ParseError(std::string reason, std::string input)
    :   KvError("parse error: " + reason + " in \"" + input + "\""),
        reason_(std::move(reason)),
        input_(std::move(input)) {}

KeyNotFoundError::KeyNotFoundError(std::string key)
    :   KvError("no such key: " + key), key_(std::move(key)) {}

TypeMismatchError::TypeMismatchError(std::string key, std::string expected, std::string actual)
    :   KvError("type mismatch for key " + key + ": expected " + expected + ", got " + actual),
        key_(std::move(key)), expected_(std::move(expected)), actual_(std::move(actual)) {}
