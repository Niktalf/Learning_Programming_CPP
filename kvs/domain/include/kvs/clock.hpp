#pragma once

#include <chrono>

namespace kvs {

// Этап 9: часы — зависимость, а не глобальная функция. Всё, что в
// хранилище измеряет интервалы (с этапа 9 — время с последней записи
// ключа, на этапе 11 — TTL), берёт время у IClock, а не у
// steady_clock::now(). В тестах вместо настоящих часов — ManualClock
// (tests/manual_clock.hpp): тест сдвигает время сам, мгновенно, и TTL на
// этапе 11 будет проверяться без единой паузы.
//
// Часы — steady, а не system. Системное время может прыгнуть, в том числе
// назад, — синхронизация NTP, ручная правка, — и интервал по нему вышел бы
// отрицательным или огромным. Steady-часы монотонны и предназначены для
// интервалов; TTL — это интервал, а не момент.
class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual std::chrono::steady_clock::time_point now() const noexcept = 0;

protected:
    IClock() = default;
    IClock(const IClock&) = default;
    IClock& operator=(const IClock&) = default;
};

// Настоящие часы. Состояния нет, поэтому один экземпляр на программу —
// steady_clock() ниже — безопасен и для потоков.
class SteadyClock final : public IClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
};

[[nodiscard]] inline const IClock& steady_clock() noexcept {
    static const SteadyClock clock;
    return clock;
}

}  // namespace kvs
