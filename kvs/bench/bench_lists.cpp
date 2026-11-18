// Этап 7, пункт 2: три представления списка на профиле нагрузки KVS.
//
//   cmake -B build-release -DCMAKE_BUILD_TYPE=Release
//   cmake --build build-release --target kvs_bench_lists
//   ./build-release/kvs_bench_lists              # таблица времени и выделений
//   valgrind --tool=cachegrind --cache-sim=yes ./build-release/kvs_bench_lists deque 100000
//                                                # промахи кэша, один вариант
//
// Профиль: много вставок в начало и в конец (LPUSH и RPUSH поровну), редкое
// чтение по индексу (LRANGE одного элемента на каждую тысячу вставок) и в
// конце — снятие всех элементов с обоих концов (LPOP и RPOP). Элементы —
// строки по 12 символов: короче 16, поэтому живут внутри std::string и сами
// памяти не выделяют. Считается только цена контейнера.
//
// Результаты и выводы — в docs/benchmarks.md.

#include <charconv>
#include <cstddef>
#include <cstring>
#include <deque>
#include <iostream>
#include <iterator>
#include <list>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "bench_util.hpp"

namespace {

// Три реализации одного интерфейса. Шаблон, а не виртуальные функции:
// виртуальный вызов добавил бы одинаковую цену всем трём и смазал разницу.
template <class Container>
struct ListModel {
    Container items;

    void push_front(std::string item) {
        if constexpr (requires { items.push_front(std::move(item)); }) {
            items.push_front(std::move(item));
        } else {
            // У vector нет push_front: вставка в начало сдвигает все
            // элементы — O(n) на каждую.
            items.insert(items.begin(), std::move(item));
        }
    }

    void push_back(std::string item) { items.push_back(std::move(item)); }

    void pop_front() {
        if constexpr (requires { items.pop_front(); }) {
            items.pop_front();
        } else {
            items.erase(items.begin());
        }
    }

    void pop_back() { items.pop_back(); }

    // Чтение по индексу. У list — std::next, проход от начала: O(n).
    [[nodiscard]] const std::string& at(std::size_t index) const {
        return *std::next(items.begin(), static_cast<std::ptrdiff_t>(index));
    }

    [[nodiscard]] std::size_t size() const noexcept { return items.size(); }
};

// Операции профиля заранее: генератор случайных чисел не должен попадать в
// замер, и все три реализации получают одну и ту же последовательность.
struct Operation {
    bool front;
    bool read;   // после вставки прочитать случайный элемент
    std::size_t index_seed;
};

std::vector<Operation> profile(std::size_t count) {
    std::mt19937_64 random(20261025);
    std::vector<Operation> operations;
    operations.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        operations.push_back(Operation{random() % 2 == 0, i % 1000 == 999, random()});
    }
    return operations;
}

std::string item_of(std::size_t i) {
    char buffer[16] = "item-";
    std::to_chars(buffer + 5, buffer + 15, 1000000 + i);
    return std::string(buffer);
}

template <class Container>
void run(const std::vector<Operation>& operations) {
    ListModel<Container> list;
    for (std::size_t i = 0; i < operations.size(); ++i) {
        const Operation& operation = operations[i];
        if (operation.front) {
            list.push_front(item_of(i));
        } else {
            list.push_back(item_of(i));
        }
        if (operation.read) {
            kvs::bench::keep(list.at(operation.index_seed % list.size()));
        }
    }
    while (list.size() > 1) {
        list.pop_front();
        list.pop_back();
    }
}

template <class Container>
void report(const char* name, std::size_t count) {
    const auto operations = profile(count);
    // Медленный vector на 10^5 вставках в начало занимает десятки секунд;
    // один прогон на этом размере — разница в порядки, и шум её не скроет.
    const int rounds = count >= 100000 ? 1 : 7;
    const auto result = kvs::bench::measure(
        rounds, [] { return 0; }, [&](int) { run<Container>(operations); });
    std::cout << "| " << name << " | " << count << " | " << result.milliseconds << " | "
              << result.allocations << " |\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    // Один вариант на одном размере — для valgrind: прогон под cachegrind
    // в десятки раз медленнее, таблицу целиком под ним не дождаться.
    if (argc == 3) {
        const std::string_view which = argv[1];
        const auto operations = profile(static_cast<std::size_t>(std::stoul(argv[2])));
        if (which == "vector") {
            run<std::vector<std::string>>(operations);
        } else if (which == "deque") {
            run<std::deque<std::string>>(operations);
        } else if (which == "list") {
            run<std::list<std::string>>(operations);
        } else {
            std::cerr << "usage: kvs_bench_lists [vector|deque|list COUNT]\n";
            return 2;
        }
        return 0;
    }
    std::cout << "| container | operations | best, ms | allocations |\n|---|---|---|---|\n";
    for (const std::size_t count : {1000u, 10000u, 100000u}) {
        report<std::vector<std::string>>("vector", count);
        report<std::deque<std::string>>("deque", count);
        report<std::list<std::string>>("list", count);
    }
    return 0;
}
