#pragma once

#include <string>

#include "command.hpp"
#include "task_store.hpp"

struct Response {
    bool ok = true;
    std::string text;
};

class Service {
public:
    explicit Service(TaskStore& store) noexcept : store_(store) {}
    [[nodiscard]] Response execute(const Command& command);
    [[nodiscard]] TaskStore& store() const noexcept { return store_; }

private:
    TaskStore& store_;
    TaskId next_id_ = 1;
};
