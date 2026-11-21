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

#include "taskq/events.hpp"
#include "taskq/task.hpp"

namespace taskq {

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
// Для сервиса задач цена повтора наглядна: без защиты счётчик выполненных
// задач задваивается, и по нему нельзя судить о проделанной работе — та же
// беда, что у самих задач, которые тоже могут выполниться дважды.
//
// Окно живёт в памяти и спасает от повторов внутри одного запуска. Повтор
// после падения приходит уже новому процессу — для него окно сохраняется в
// файл (taskq_tail --state) до фиксации смещения и читается при запуске.
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

// Обнаружение пропусков. Издатель нумерует события каждой задачи подряд:
// 1, 2, 3… Если после 2 пришло 4, событие потеряно — например, его отбросил
// сервис при переполненной очереди (docs/events.md). Подписчику, который
// по событиям запускает следующий шаг обработки, это важно знать: пропавшее
// TaskCompleted значит, что следующий шаг не запустится.
//
// Задача, которой потребитель ещё не видел, и новый издатель — после
// перезапуска сервиса номера начинаются заново — пропуском не считаются.
// Завершённая задача забывается: после последнего события переходов нет.
class SequenceTracker {
public:
    struct Gap {
        std::uint64_t expected;
        std::uint64_t got;
    };

    [[nodiscard]] std::optional<Gap> observe(std::string_view publisher, TaskId task, std::uint64_t number,
                                             bool final) {
        const auto [it, inserted] = last_.try_emplace(task, std::string(publisher), number);
        std::optional<Gap> gap;
        if (!inserted) {
            auto& [known_publisher, last] = it->second;
            if (known_publisher == publisher && number != last + 1) {
                gap = Gap{last + 1, number};
            }
            known_publisher = publisher;
            last = number;
        }
        if (final) {
            last_.erase(it);
        }
        return gap;
    }

    // Сколько задач отслеживается — для тестов: завершённые не копятся.
    [[nodiscard]] std::size_t tracked() const noexcept { return last_.size(); }

private:
    // Задача → издатель и последний виденный номер.
    std::unordered_map<TaskId, std::pair<std::string, std::uint64_t>> last_;
};

// Итоги потока: сколько задач поставлено, выполнено, провалено. Ради них
// потребитель и защищается от повторов — повторное TaskCompleted не должно
// засчитаться второй выполненной задачей.
struct Tally {
    std::uint64_t submitted = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t duplicates = 0;   // пропущенные повторы

    void count(const DomainEvent& event) noexcept {
        if (std::holds_alternative<TaskSubmitted>(event)) {
            ++submitted;
        } else if (std::holds_alternative<TaskCompleted>(event)) {
            ++completed;
        } else if (std::holds_alternative<TaskFailed>(event)) {
            ++failed;
        } else if (std::holds_alternative<TaskCancelled>(event)) {
            ++cancelled;
        }
    }
};

}  // namespace taskq
