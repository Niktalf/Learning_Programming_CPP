#pragma once

#include <string>

#include "command.hpp"
#include "store.hpp"

struct Response {
    bool ok = true;
    std::string text;
};

[[nodiscard]] Response execute(Store& store, const Command& command);
