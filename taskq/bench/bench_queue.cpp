// Этап 7, пункт 2: три представления очереди задач с приоритетной выборкой.
//
//   cmake -B build-release -DCMAKE_BUILD_TYPE=Release
//   cmake --build build-release --target taskq_bench_queue
//   ./build-release/taskq_bench_queue                  # таблица времени и выделений
//   valgrind --tool=cachegrind --cache-sim=yes ./build-release/taskq_bench_queue map 1000
//                                                      # промахи кэша, один вариант
//
// Профиль: в очереди постоянно около Q задач (заполняется заранее, в замер
// не входит). Затем 200 000 операций: 48 % — постановка с приоритетом 0..9,
// 48 % — выборка с вершины (наибольший приоритет, при равном — раньше
// поставленная), 4 % — удаление произвольной ещё не выданной задачи по
// номеру. Порядок операций и номера удаляемых задач заданы заранее и
// одинаковы для всех вариантов.
//
// Задача в замере — номер, а ключ — приоритет и порядковый номер, как
// TaskKey. Ключ удаляемой задачи вызывающему известен: на настоящем сервисе
// его даёт индекс по номеру (пункт 3 этапа).
//
// Результаты и выводы — в docs/benchmarks.md.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bench_util.hpp"

namespace {

using Id = std::uint64_t;

// Ключ выборки: «меньше» — раньше выдать. Больший приоритет раньше, при
// равном — меньший порядковый номер.
struct Key {
    int priority;
    std::uint64_t seq;

    friend bool operator<(const Key& a, const Key& b) noexcept {
        return a.priority != b.priority ? a.priority > b.priority : a.seq < b.seq;
    }
};

struct Item {
    Key key;
    Id id;
};

// priority_queue. Удаления произвольного элемента у неё нет вовсе: доступна
// только вершина. Единственный обходной путь — ленивое удаление: номер
// помечается удалённым, а сама задача остаётся в куче и выбрасывается, когда
// доберётся до вершины. Плата — память под мёртвые задачи и таблица пометок.
class Heap {
public:
    void push(Key key, Id id) { heap_.push(Item{key, id}); }

    Id pop() {
        while (true) {
            const Id id = heap_.top().id;
            heap_.pop();
            if (cancelled_.erase(id) == 0) {
                return id;
            }
        }
    }

    void remove(Key, Id id) { cancelled_.insert(id); }

private:
    // priority_queue выдаёт наибольший по сравнению, а у нас «меньше —
    // раньше», поэтому сравнение перевёрнуто.
    struct Later {
        bool operator()(const Item& a, const Item& b) const noexcept { return b.key < a.key; }
    };
    std::priority_queue<Item, std::vector<Item>, Later> heap_;
    std::unordered_set<Id> cancelled_;
};

// Отсортированный vector — по убыванию, чтобы вершина была в конце и
// выборка стоила pop_back. Вставка и удаление — двоичный поиск места и
// сдвиг хвоста: O(n).
class SortedVector {
public:
    void push(Key key, Id id) {
        const auto at = std::upper_bound(items_.begin(), items_.end(), key,
                                         [](const Key& k, const Item& item) { return item.key < k; });
        items_.insert(at, Item{key, id});
    }

    Id pop() {
        const Id id = items_.back().id;
        items_.pop_back();
        return id;
    }

    void remove(Key key, Id) {
        const auto at = std::lower_bound(items_.begin(), items_.end(), key,
                                         [](const Item& item, const Key& k) { return k < item.key; });
        items_.erase(at);
    }

private:
    std::vector<Item> items_;   // по убыванию ключа: вершина — back()
};

// multimap по ключу. Ключ здесь уникален — порядковый номер не повторяется,
// — так что это то же дерево, что и map; в сервисе так и будет.
class Tree {
public:
    void push(Key key, Id id) { items_.emplace(key, id); }

    Id pop() {
        const auto top = items_.begin();
        const Id id = top->second;
        items_.erase(top);
        return id;
    }

    void remove(Key key, Id) { items_.erase(items_.find(key)); }

private:
    std::multimap<Key, Id> items_;
};

enum class Kind { Push, Pop, Remove };

struct Operation {
    Kind kind;
    int priority;
};

constexpr std::size_t operations_count = 200000;

// Профиль заранее. Чтобы удалять только существующие задачи, профиль
// прогоняется на модели, и для каждого удаления запоминается, какую задачу
// удалять. Модель — дерево живых ключей (для выборки с вершины) и вектор
// тех же ключей для случайного выбора за O(1): удаляемая задача — любая из
// ожидающих, а не только у вершины.
struct Profile {
    std::vector<Item> prefill;
    std::vector<Operation> operations;
    std::vector<Item> removals;
};

class Model {
public:
    void add(Item item) {
        position_.emplace(item.id, items_.size());
        items_.push_back(item);
        ordered_.emplace(item.key, item.id);
    }

    void pop_top() { erase(ordered_.begin()->second); }

    Item pick(std::uint64_t random) const { return items_[random % items_.size()]; }

    void erase(Id id) {
        const std::size_t at = position_.at(id);
        ordered_.erase(items_[at].key);
        // Удаление из вектора обменом с последним — O(1), порядок не важен.
        items_[at] = items_.back();
        position_[items_[at].id] = at;
        items_.pop_back();
        position_.erase(id);
    }

    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

private:
    std::vector<Item> items_;
    std::unordered_map<Id, std::size_t> position_;
    std::map<Key, Id> ordered_;
};

Profile make_profile(std::size_t queue_size) {
    std::mt19937_64 random(20261025);
    Profile profile;
    Model live;
    std::uint64_t seq = 0;
    const auto next = [&] {
        ++seq;
        return Item{Key{static_cast<int>(random() % 10), seq}, seq};
    };
    for (std::size_t i = 0; i < queue_size; ++i) {
        const Item item = next();
        live.add(item);
        profile.prefill.push_back(item);
    }
    for (std::size_t i = 0; i < operations_count; ++i) {
        const auto roll = random() % 100;
        if (roll < 48 || live.empty()) {
            const Item item = next();
            live.add(item);
            profile.operations.push_back(Operation{Kind::Push, item.key.priority});
        } else if (roll < 96) {
            live.pop_top();
            profile.operations.push_back(Operation{Kind::Pop, 0});
        } else {
            const Item victim = live.pick(random());
            live.erase(victim.id);
            profile.removals.push_back(victim);
            profile.operations.push_back(Operation{Kind::Remove, 0});
        }
    }
    return profile;
}

template <class Queue>
Queue prepared(const Profile& profile) {
    Queue queue;
    for (const Item& item : profile.prefill) {
        queue.push(item.key, item.id);
    }
    return queue;
}

template <class Queue>
void run(Queue& queue, const Profile& profile) {
    std::uint64_t seq = profile.prefill.size();
    std::size_t next_removal = 0;
    Id sum = 0;
    for (const Operation& operation : profile.operations) {
        switch (operation.kind) {
            case Kind::Push:
                ++seq;
                queue.push(Key{operation.priority, seq}, seq);
                break;
            case Kind::Pop:
                sum += queue.pop();
                break;
            case Kind::Remove: {
                const Item& victim = profile.removals[next_removal++];
                queue.remove(victim.key, victim.id);
                break;
            }
        }
    }
    taskq::bench::keep(sum);
}

template <class Queue>
void report(const char* name, std::size_t queue_size) {
    const Profile profile = make_profile(queue_size);
    // Отсортированный vector на 10^5 задач — секунды на прогон; одного
    // прогона на этом размере достаточно, разница — в порядки.
    const int rounds = queue_size >= 100000 ? 1 : 5;
    const auto result = taskq::bench::measure(
        rounds, [&] { return prepared<Queue>(profile); }, [&](Queue& queue) { run(queue, profile); });
    std::cout << "| " << name << " | " << queue_size << " | " << result.milliseconds << " | "
              << result.allocations << " |\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 3) {
        const std::string_view which = argv[1];
        const Profile profile = make_profile(static_cast<std::size_t>(std::stoul(argv[2])));
        if (which == "heap") {
            auto queue = prepared<Heap>(profile);
            run(queue, profile);
        } else if (which == "vector") {
            auto queue = prepared<SortedVector>(profile);
            run(queue, profile);
        } else if (which == "map") {
            auto queue = prepared<Tree>(profile);
            run(queue, profile);
        } else {
            std::cerr << "usage: taskq_bench_queue [heap|vector|map QUEUE_SIZE]\n";
            return 2;
        }
        return 0;
    }
    std::cout << "200 000 operations: 48% submit, 48% poll, 4% remove by id\n\n"
              << "| queue | tasks waiting | best, ms | allocations |\n|---|---|---|---|\n";
    for (const std::size_t queue_size : {100u, 10000u, 100000u}) {
        report<Heap>("priority_queue", queue_size);
        report<SortedVector>("sorted vector", queue_size);
        report<Tree>("multimap", queue_size);
    }
    return 0;
}
