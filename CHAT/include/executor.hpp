#pragma once

#include <string>

#include "chat_store.hpp"
#include "command.hpp"

struct Response {
    bool ok = true;
    std::string text;
};

[[nodiscard]] Response execute(ChatStore& store, const Command& command);
