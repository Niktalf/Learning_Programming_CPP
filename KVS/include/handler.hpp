#pragma once

#include <string>

#include "executor.hpp"
#include "store.hpp"

[[nodiscard]] Response handle(Store& store, std::string line);


