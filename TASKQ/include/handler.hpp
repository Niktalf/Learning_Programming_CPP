#pragma once

#include <string>

#include "executor.hpp"

[[nodiscard]] Response handle(Service& service, std::string line);
