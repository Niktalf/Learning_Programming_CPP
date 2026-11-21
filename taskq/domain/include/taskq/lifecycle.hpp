#pragma once

#include <map>
#include <string>
#include <utility>

#include "taskq/errors.hpp"
#include "taskq/state.hpp"
#include "taskq/task.hpp"

namespace taskq {

// Этап 9: состояния задач и проверка переходов — отдельная обязанность.
// До этапа 9 они жили в Service рядом с арендами, воркерами, журналом и
// командами: поле states_ и три функции state_of, require_transition,
// transition_to. А правило «эта команда — только для ожидающей задачи»
// было написано в BUMP и DELAY двумя одинаковыми ветками с WrongStateError
// (SRP и DRY, docs/audit.md, записи 1 и 6).
//
// Теперь все вопросы «можно ли» задаются здесь — проверки переходов в одной
// точке, и таблица allowed() из state.hpp — единственный источник правды:
// диаграмма состояний docs/audit/states.puml сверяется с ней.
class Lifecycle {
public:
    // Текущее состояние; задачи, которой не было, — UnknownTaskError.
    [[nodiscard]] State state_of(TaskId id) const {
        const auto it = states_.find(id);
        if (it == states_.end()) {
            throw UnknownTaskError(id);
        }
        return it->second;
    }

    // Проверка перехода по автомату — до любых изменений.
    void require(TaskId id, State to) const {
        const State from = state_of(id);
        if (!allowed(from, to)) {
            throw InvalidTransitionError(id, from, to);
        }
    }

    // Команда, которой нужна задача в определённом состоянии (BUMP и DELAY
    // — только ожидающая). Возвращает состояние — оно и так прочитано.
    State require_state(TaskId id, State expected, std::string operation) const {
        const State state = state_of(id);
        if (state != expected) {
            throw WrongStateError(id, state, std::move(operation));
        }
        return state;
    }

    // Фиксация перехода, уже проверенного require. Не бросает: запись
    // существует, присваивание перечисления бросить не может.
    void commit(TaskId id, State to) noexcept { states_.find(id)->second = to; }

    // Новые задачи. merge переносит узлы, не выделяя памяти, и бросить может
    // только сравнение ключей — а сравнение целых не бросает.
    void admit(std::map<TaskId, State>& prepared) noexcept { states_.merge(prepared); }

private:
    std::map<TaskId, State> states_;
};

}  // namespace taskq
