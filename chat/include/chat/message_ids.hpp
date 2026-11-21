#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "chat/clock.hpp"
#include "chat/message_id.hpp"

namespace chat {

// Этап 9: выдача идентификаторов сообщений — отдельная обязанность. До
// этапа 9 её делало хранилище: поле seq_, лямбда с system_clock::now() и
// функция next_message_id среди операций над комнатами (SRP,
// docs/audit.md, запись 1). У неё своя причина меняться: на этапе 12
// счётчик станет атомарным, и разбираться с порядком памяти придётся здесь,
// а не в хранилище.
//
// Номер — следующий в общей последовательности чата, время — системное
// (IClock::wall()), в миллисекундах от эпохи: его видит пользователь.
class MessageIdSource {
public:
    explicit MessageIdSource(const IClock& clock) noexcept : clock_(&clock) {}

    [[nodiscard]] MessageId next(std::string author) {
        const auto wall = std::chrono::duration_cast<std::chrono::milliseconds>(clock_->wall().time_since_epoch());
        return MessageId{++seq_, static_cast<std::uint64_t>(wall.count()), std::move(author)};
    }

private:
    const IClock* clock_;     // не во владении — живут у точки сборки
    std::uint64_t seq_ = 0;   // последний выданный номер
};

}  // namespace chat
