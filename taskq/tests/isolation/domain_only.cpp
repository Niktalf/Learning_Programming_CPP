// Этап 10: проверка изолированной сборки домена.
//
// Программа линкуется только с taskq_domain — ни application, ни
// infrastructure, ни обёртки брокера (CMakeLists.txt, taskq_domain_isolated).
// Индекс задач — свой, на std::map, написанный прямо здесь: настоящие
// индексы живут в infrastructure, и их здесь нет. Если домен начнёт зависеть
// от внешнего слоя, эта цель перестанет собираться или линковаться.
//
// Здесь же — автомат состояний: он обязан быть в домене, и проверка его
// переходов не требует ничего, кроме домена.
//
// Удобные конструкторы BasicTaskStore() и BasicTaskStore(EngineKind) не
// используются намеренно: они зовут фабрику make_task_index, которая
// объявлена в домене, а определена в infrastructure (docs/layers.md).

#include <cstdio>
#include <map>
#include <memory>
#include <string_view>

#include "taskq/events.hpp"
#include "taskq/lifecycle.hpp"
#include "taskq/state.hpp"
#include "taskq/task_index.hpp"
#include "taskq/task_store.hpp"

namespace {

using Location = taskq::TaskStore::IndexEntry;

class MapIndex final : public taskq::ITaskIndex<Location> {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "map"; }
    [[nodiscard]] Location* find(taskq::TaskId id) noexcept override {
        const auto it = table_.find(id);
        return it == table_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const Location* find(taskq::TaskId id) const noexcept override {
        const auto it = table_.find(id);
        return it == table_.end() ? nullptr : &it->second;
    }
    bool insert(taskq::TaskId id, Location location) override { return table_.emplace(id, location).second; }
    bool erase(taskq::TaskId id) noexcept override { return table_.erase(id) != 0; }
    [[nodiscard]] std::size_t size() const noexcept override { return table_.size(); }
    void reserve(std::size_t) override {}

private:
    std::map<taskq::TaskId, Location> table_;
};

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    taskq::TaskStore store(std::make_unique<MapIndex>());
    static_cast<void>(store.create_queue("jobs"));
    store.submit("jobs", taskq::Task(1, std::string("echo hi"), 5));
    expect(store.find(1).has_value(), "task is indexed");

    taskq::Lifecycle states;
    std::map<taskq::TaskId, taskq::State> added{{1, taskq::State::Pending}};
    states.admit(added);
    states.require(1, taskq::State::Leased);
    states.commit(1, taskq::State::Leased);
    bool refused = false;
    try {
        states.require(1, taskq::State::Cancelled);
    } catch (const taskq::InvalidTransitionError&) {
        refused = true;
    }
    expect(refused, "a leased task cannot be cancelled");

    const taskq::DomainEvent done = taskq::TaskCompleted{1};
    expect(taskq::task_of(done) == 1 && taskq::is_final(done), "completion is the last event");
    expect(!taskq::is_final(taskq::TaskReturned{1, "released"}), "a returned task goes on");
    if (failures == 0) {
        std::puts("domain builds and runs alone");
    }
    return failures == 0 ? 0 : 1;
}
