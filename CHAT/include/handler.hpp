#pragma once

#include <string>

#include "chat_store.hpp"
#include "executor.hpp"

[[nodiscard]] Response handle(ChatStore& store, std::string line);
