#pragma once

#include <chrono>

#include "kvs/clock.hpp"

namespace kvs::testing {

// Подставные часы: время стоит, пока тест не сдвинет его сам. Ни одной
// реальной паузы — тест на «прошло десять минут» выполняется мгновенно.
class ManualClock final : public IClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point now() const noexcept override { return now_; }
    void advance(std::chrono::seconds by) noexcept { now_ += by; }

private:
    std::chrono::steady_clock::time_point now_{};
};

}  // namespace kvs::testing
