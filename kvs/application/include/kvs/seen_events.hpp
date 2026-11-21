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
#include <variant>

#include "kvs/events.hpp"
#include "kvs/overloaded.hpp"

namespace kvs {

// Звёздочка этапа 10: идемпотентный потребитель.
//
// Брокер в выданной обёртке даёт at-least-once: смещение фиксируется после
// обработки (commit), и если потребитель упадёт между обработкой и
// фиксацией, после перезапуска те же сообщения придут снова. Издатель тоже
// может отправить одно событие дважды при повторе после сбоя сети. Повторы
// неизбежны, и защищаться от них — дело потребителя: по идентификатору
// события (event_wire.hpp) он узнаёт уже обработанное и пропускает.
//
// Множество ограничено: помнить все идентификаторы вечно — та же
// неограниченная память, от которой предостерегает домашнее задание.
// Хранится последних capacity штук, старейший вытесняется первым. Повтор,
// пришедший позже, чем через capacity событий, пройдёт — это цена границы,
// и она приемлема: повторы приходят пачкой сразу после перезапуска, а не
// через сотни тысяч сообщений.
//
// Окно живёт в памяти, и само по себе спасает от повторов внутри одного
// запуска: повторной отправки издателем, перебалансировки группы. Повтор
// после падения приходит уже новому процессу — для него окно сохраняется в
// файл (kvs_tail --state) до фиксации смещения и читается при запуске.
//
// Работает в прикладном слое потребителя: ничего не знает о брокере и
// проверяется модульными тестами.
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

    // Запомненные идентификаторы от старейшего к новейшему — чтобы
    // сохранить окно между запусками (kvs_tail --state).
    [[nodiscard]] const std::deque<std::string>& ids() const noexcept { return order_; }

private:
    std::size_t capacity_;
    std::deque<std::string> order_;          // порядок вытеснения
    std::unordered_set<std::string> seen_;   // быстрый поиск
};

// Обнаружение пропусков. Поколения одного ключа идут подряд: 1, 2, 3… —
// и после удаления начинаются заново. Если после поколения 3 пришло 5,
// событие потеряно: например, его отбросил сервер при переполнении очереди
// (docs/events.md). Потребитель, устойчивый к пропускам, должен хотя бы
// знать о них.
//
// Ключ, которого потребитель ещё не видел, пропуском не считается: он мог
// подключиться посреди потока.
class GenerationTracker {
public:
    struct Gap {
        std::uint64_t expected;
        std::uint64_t got;
    };

    [[nodiscard]] std::optional<Gap> observe(const DomainEvent& event) {
        return std::visit(
            overloaded{
                [this](const KeyChanged& e) -> std::optional<Gap> {
                    const auto [it, inserted] = last_.try_emplace(e.key, e.generation);
                    if (inserted) {
                        return std::nullopt;   // ключ впервые на глазах у потребителя
                    }
                    const std::uint64_t expected = it->second + 1;
                    it->second = e.generation;
                    if (e.generation == expected) {
                        return std::nullopt;
                    }
                    return Gap{expected, e.generation};
                },
                [this](const KeyDeleted& e) -> std::optional<Gap> { return forget(e.key); },
                [this](const KeyExpired& e) -> std::optional<Gap> { return forget(e.key); },
            },
            event);
    }

private:
    // Удалённый ключ помнится с поколением 0: следующая запись обязана
    // начаться с первого. Память — по записи на каждый виденный ключ; для
    // учебного потребителя этого достаточно.
    std::optional<Gap> forget(const std::string& key) {
        last_.insert_or_assign(key, 0);
        return std::nullopt;
    }

    std::unordered_map<std::string, std::uint64_t> last_;
};

}  // namespace kvs
