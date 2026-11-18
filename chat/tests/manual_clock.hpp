#pragma once

#include <chrono>

#include "chat/clock.hpp"

namespace chat::testing {

// Подставные часы: время стоит, пока тест не сдвинет его сам. advance двигает
// и монотонные, и системные часы; set_wall ставит системные на нужный
// момент — тестам истории по времени нужны известные отметки.
class ManualClock final : public IClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point now() const noexcept override { return steady_; }
    [[nodiscard]] std::chrono::system_clock::time_point wall() const noexcept override { return wall_; }

    void advance(std::chrono::milliseconds by) noexcept {
        steady_ += by;
        wall_ += by;
    }
    void set_wall_ms(long long ms) noexcept {
        wall_ = std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
    }

private:
    std::chrono::steady_clock::time_point steady_{};
    std::chrono::system_clock::time_point wall_{};
};

}  // namespace chat::testing
