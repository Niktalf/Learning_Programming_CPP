#pragma once

#include <chrono>

namespace chat {

// Этап 9: часы — зависимость, а не глобальная функция. Хранилище берёт время
// у IClock, а не у system_clock::now(); в тестах вместо настоящих часов —
// ManualClock (tests/manual_clock.hpp), и тест сдвигает время сам,
// мгновенно.
//
// Двое часов, потому что у чата два разных вопроса о времени:
//
//   - wall() — системное время, «который час»: отметка сообщения, которую
//     видит пользователь и по которой работают /since и /until. Ему нужно
//     совпадать с часами на стене, и это system_clock;
//   - now() — монотонное время, «сколько прошло»: простой пользователя для
//     /whois, на этапе 11 — отключение неактивных сессий. Это интервал, и
//     системное время для него не годится: при синхронизации оно может
//     прыгнуть назад, и пользователь окажется «активным из будущего».
//     Поэтому steady_clock.
class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual std::chrono::steady_clock::time_point now() const noexcept = 0;
    [[nodiscard]] virtual std::chrono::system_clock::time_point wall() const noexcept = 0;

protected:
    IClock() = default;
    IClock(const IClock&) = default;
    IClock& operator=(const IClock&) = default;
};

// Настоящие часы. Состояния нет, поэтому один экземпляр на программу —
// system_clocks() ниже — безопасен и для потоков.
class SystemClocks final : public IClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
    [[nodiscard]] std::chrono::system_clock::time_point wall() const noexcept override {
        return std::chrono::system_clock::now();
    }
};

[[nodiscard]] inline const IClock& system_clocks() noexcept {
    static const SystemClocks clock;
    return clock;
}

}  // namespace chat
