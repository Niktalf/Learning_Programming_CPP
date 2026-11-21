#pragma once

#include <chrono>

#include "taskq/clock.hpp"

namespace taskq::testing {

// Подставные часы: время стоит, пока тест не сдвинет его сам. advance двигает
// и монотонные, и системные часы. Ни одной реальной паузы — тест «аренда
// истекла через десять минут» выполняется мгновенно.
class ManualClock final : public IClock {
public:
    [[nodiscard]] Instant now() const noexcept override { return steady_; }
    [[nodiscard]] std::chrono::system_clock::time_point wall() const noexcept override { return wall_; }

    void advance(std::chrono::seconds by) noexcept {
        steady_ += by;
        wall_ += by;
    }
    void set_wall(std::chrono::system_clock::time_point at) noexcept { wall_ = at; }

private:
    Instant steady_{};
    std::chrono::system_clock::time_point wall_{};
};

}  // namespace taskq::testing
