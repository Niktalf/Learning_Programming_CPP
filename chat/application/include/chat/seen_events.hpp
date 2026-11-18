#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace chat {

// Звёздочка этапа 10: идемпотентный потребитель.
//
// Брокер в выданной обёртке даёт at-least-once: смещение фиксируется после
// обработки (commit), и если потребитель упадёт между обработкой и
// фиксацией, после перезапуска те же сообщения придут снова. Издатель тоже
// может отправить одно событие дважды при повторе после сбоя сети. Повторы
// неизбежны, и защищаться от них — дело потребителя: по идентификатору
// события он узнаёт уже обработанное и пропускает.
//
// Множество ограничено: помнить все идентификаторы вечно — та же
// неограниченная память, от которой предостерегает домашнее задание.
// Хранится последних capacity штук, старейший вытесняется первым.
//
// Окно живёт в памяти и спасает от повторов внутри одного запуска. Повтор
// после падения приходит уже новому процессу — для него окно сохраняется в
// файл (chat_tail --state) до фиксации смещения и читается при запуске.
//
// Прикладной слой потребителя: ничего не знает о брокере и проверяется
// модульными тестами.
class SeenEvents {
public:
    explicit SeenEvents(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    // true — событие новое и запомнено; false — повтор.
    [[nodiscard]] bool first_time(std::string_view id) {
        std::string owned(id);
        if (seen_.contains(owned)) {
            return false;
        }
        if (order_.size() == capacity_) {
            seen_.erase(order_.front());
            order_.pop_front();
        }
        order_.push_back(owned);
        seen_.insert(std::move(owned));
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return order_.size(); }

    // Запомненные идентификаторы от старейшего к новейшему — чтобы сохранить
    // окно между запусками.
    [[nodiscard]] const std::deque<std::string>& ids() const noexcept { return order_; }

private:
    std::size_t capacity_;
    std::deque<std::string> order_;          // порядок вытеснения
    std::unordered_set<std::string> seen_;   // быстрый поиск
};

// Обнаружение пропусков. Издатель нумерует события каждой комнаты подряд:
// 1, 2, 3… Если после 3 пришло 5, событие потеряно — например, его отбросил
// сервер при переполненной очереди (docs/events.md). Для чата это значит
// дыру в ленте: подписчик, которому лента нужна целиком, должен дочитать её
// командой /history.
//
// Комната, которой потребитель ещё не видел, и новый издатель — после
// перезапуска сервера номера начинаются заново — пропуском не считаются.
class SequenceTracker {
public:
    struct Gap {
        std::uint64_t expected;
        std::uint64_t got;
    };

    [[nodiscard]] std::optional<Gap> observe(std::string_view publisher, const std::string& room,
                                             std::uint64_t number) {
        const auto [it, inserted] = last_.try_emplace(room, std::string(publisher), number);
        if (inserted) {
            return std::nullopt;
        }
        auto& [known_publisher, last] = it->second;
        if (known_publisher != publisher) {
            known_publisher = publisher;
            last = number;
            return std::nullopt;
        }
        const std::uint64_t expected = last + 1;
        last = number;
        if (number == expected) {
            return std::nullopt;
        }
        return Gap{expected, number};
    }

private:
    // Комната → издатель и последний виденный номер.
    std::unordered_map<std::string, std::pair<std::string, std::uint64_t>> last_;
};

}  // namespace chat
