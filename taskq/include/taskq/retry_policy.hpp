#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace taskq {

// Домашнее задание этапа 9: Pure Fabrication (GRASP).
//
// Что делать с задачей, которую воркер провалил (FAIL): сколько раз пробовать
// ещё, через сколько, когда сдаться. Это не задача — задача не знает, сколько
// раз её пробовали и сколько ещё разрешено, — и не очередь: очередь хранит и
// выдаёт, а не решает. Обязанность, которую обычно размазывают по обоим:
// счётчик попыток в задаче, задержка в очереди, «сдаться» в обработчике FAIL.
// RetryPolicy — придуманный класс: в предметной области такой сущности нет,
// он появился, чтобы у этой обязанности был один хозяин.
//
// Политика по умолчанию — без повторов: первая же неудача окончательна, как
// и до этапа 9. Повторы включаются при запуске: --retries=3 --backoff=10.
class RetryPolicy {
public:
    RetryPolicy() = default;

    // attempts — всего попыток, включая первую; backoff — пауза перед первым
    // повтором, дальше каждая вдвое длиннее предыдущей.
    RetryPolicy(std::uint32_t attempts, std::chrono::seconds backoff) noexcept
        : attempts_(attempts == 0 ? 1 : attempts), backoff_(backoff) {}

    // Через сколько повторить задачу, проваленную в failed-й раз (с 1), или
    // пусто — попытки исчерпаны, задача провалена окончательно.
    [[nodiscard]] std::optional<std::chrono::seconds> retry_after(std::uint32_t failed) const noexcept {
        if (failed >= attempts_) {
            return std::nullopt;
        }
        // Экспоненциальная пауза; сдвиг ограничен, чтобы не переполниться.
        const std::uint32_t doublings = failed - 1 < 20 ? failed - 1 : 20;
        return backoff_ * (std::int64_t{1} << doublings);
    }

    [[nodiscard]] std::uint32_t attempts() const noexcept { return attempts_; }

private:
    std::uint32_t attempts_ = 1;
    std::chrono::seconds backoff_{0};
};

}  // namespace taskq
