// Этап 8, пункт 6: один и тот же замер на каждом движке индекса по номеру.
//
//   ./build-release/taskq_bench_engines
//
// Через хранилище задач — так, как с индексом работают команды: N
// постановок в одну очередь со случайными приоритетами, поиск каждой задачи
// по номеру в случайном порядке (STATUS, ACK), N поисков отсутствующих
// номеров, выдача всех задач (POLL — каждая выдача удаляет запись индекса)
// и «текучка» — окно из N задач, через которое проходят ещё N: выдать
// старейшую, поставить новую.
//
// Результаты и честный вывод — docs/benchmarks.md.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include "bench_util.hpp"
#include "taskq/task_store.hpp"

namespace {

using taskq::Task;
using taskq::TaskId;
using taskq::TaskStore;

void report(taskq::EngineKind kind, std::size_t count) {
    std::mt19937 generator(1);
    std::uniform_int_distribution<int> priority(0, 9);
    std::vector<int> priorities(count);
    std::ranges::generate(priorities, [&] { return priority(generator); });
    std::vector<TaskId> lookups(count);
    std::ranges::generate(lookups, [next = TaskId{0}]() mutable { return ++next; });
    std::ranges::shuffle(lookups, generator);
    const int rounds = count >= 1000000 ? 2 : 5;

    const auto make = [kind] {
        TaskStore store(kind);
        store.create_queue("q");
        return store;
    };
    const auto fill = [&](TaskStore& store) {
        for (std::size_t i = 0; i < count; ++i) {
            store.submit("q", Task(i + 1, "x", priorities[i]));
        }
    };
    const auto submit = taskq::bench::measure(rounds, make, fill);

    TaskStore filled = make();
    fill(filled);
    const auto hit = taskq::bench::measure(rounds, [] { return 0; }, [&](int) {
        std::size_t found = 0;
        for (const TaskId id : lookups) {
            found += filled.find(id) ? 1 : 0;
        }
        taskq::bench::keep(found);
    });
    const auto miss = taskq::bench::measure(rounds, [] { return 0; }, [&](int) {
        std::size_t found = 0;
        for (const TaskId id : lookups) {
            found += filled.find(id + count) ? 1 : 0;   // номера, которых ещё не было
        }
        taskq::bench::keep(found);
    });
    const auto take = taskq::bench::measure(
        rounds, [&] { TaskStore store = make(); fill(store); return store; },
        [&](TaskStore& store) {
            while (store.take("q")) {
            }
        });
    const auto churn = taskq::bench::measure(
        1, [&] { TaskStore store = make(); fill(store); return store; },
        [&](TaskStore& store) {
            for (std::size_t i = 0; i < count; ++i) {
                static_cast<void>(store.take("q"));
                store.submit("q", Task(count + i + 1, "x", priorities[i]));
            }
        });
    std::cout << "| " << taskq::name_of(kind) << " | " << count << " | " << submit.milliseconds << " | "
              << hit.milliseconds << " | " << miss.milliseconds << " | " << take.milliseconds << " | "
              << churn.milliseconds << " |\n";
}

}  // namespace

int main() {
    std::cout << "| engine | tasks | submit, ms | find id, ms | miss id, ms | poll all, ms | churn, ms |\n"
                 "|---|---|---|---|---|---|---|\n";
    for (const std::size_t count : {1000u, 100000u, 1000000u}) {
        for (const auto kind : {taskq::EngineKind::Std, taskq::EngineKind::Hash, taskq::EngineKind::Open}) {
            report(kind, count);
        }
    }
    // Плохая хеш-функция — отдельно и на меньших размерах.
    for (const std::size_t count : {1000u, 10000u, 30000u}) {
        report(taskq::EngineKind::Hash, count);
        report(taskq::EngineKind::BadHash, count);
    }
    return 0;
}
