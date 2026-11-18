#pragma once

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "chat/events.hpp"

namespace chat::testing {

// Подставной издатель (этап 10): запоминает опубликованное. События общие у
// всех копий — сервер владеет своей через unique_ptr, тест смотрит через
// свою, как и у MemoryLog. Брокер не нужен: тесты сценариев проходят при
// остановленном стенде.
class FakePublisher final : public IEventPublisher {
public:
    void publish(const DomainEvent& event) override {
        if (*failing_) {
            throw std::runtime_error("broker is down");
        }
        events_->push_back(event);
    }

    [[nodiscard]] const std::vector<DomainEvent>& events() const noexcept { return *events_; }
    void clear() noexcept { events_->clear(); }

    // Издатель начинает бросать — как адаптер при переполненной очереди.
    void fail(bool on) noexcept { *failing_ = on; }

    [[nodiscard]] std::unique_ptr<IEventPublisher> port() const { return std::make_unique<FakePublisher>(*this); }

private:
    std::shared_ptr<std::vector<DomainEvent>> events_ = std::make_shared<std::vector<DomainEvent>>();
    std::shared_ptr<bool> failing_ = std::make_shared<bool>(false);
};

}  // namespace chat::testing
