#pragma once

#include <chrono>

namespace taskq {

// Монотонный момент — для сроков: аренды, отложенного запуска.
using Instant = std::chrono::steady_clock::time_point;

// Этап 9: часы — зависимость, а не std::function с system_clock внутри.
// Сервис и хранилище берут время у IClock; в тестах вместо настоящих часов —
// ManualClock (tests/manual_clock.hpp), и тест истечения аренды сдвигает
// время на десять минут мгновенно.
//
// Двое часов, потому что у очереди два разных вопроса о времени:
//
//   - now() — монотонное время, «сколько прошло»: срок аренды и момент
//     отложенного запуска. Это интервалы. До этапа 9 они считались по
//     системным часам, и это было ошибкой: при переводе часов назад аренда
//     не истекла бы никогда, вперёд — истекла бы мгновенно (docs/audit.md,
//     запись 8);
//   - wall() — системное время, «который час»: когда задача поставлена. Его
//     показывают пользователю (INFO), и ему нужно совпадать с часами на
//     стене.
class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual Instant now() const noexcept = 0;
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
    [[nodiscard]] Instant now() const noexcept override { return std::chrono::steady_clock::now(); }
    [[nodiscard]] std::chrono::system_clock::time_point wall() const noexcept override {
        return std::chrono::system_clock::now();
    }
};

[[nodiscard]] inline const IClock& system_clocks() noexcept {
    static const SystemClocks clock;
    return clock;
}

}  // namespace taskq
