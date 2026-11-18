#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "alloc_counter.hpp"

namespace kvs::bench {

// Результат одного замера: время лучшего из нескольких прогонов и число
// выделений памяти в одном прогоне. Лучший, а не средний: шум — фоновые
// процессы, промахи TLB, частота процессора — только добавляет время, и
// минимум ближе всего к стоимости самой операции.
struct Measurement {
    double milliseconds = 0;
    std::size_t allocations = 0;
};

// Прогон run повторяется rounds раз; перед каждым — подготовка prepare, она
// в замер не входит.
template <class Prepare, class Run>
Measurement measure(int rounds, Prepare prepare, Run run) {
    Measurement best{1e300, 0};
    for (int round = 0; round < rounds; ++round) {
        auto state = prepare();
        tools::reset_allocations();
        const auto start = std::chrono::steady_clock::now();
        run(state);
        const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
        best.allocations = tools::allocations();
        best.milliseconds = std::min(best.milliseconds, elapsed.count());
    }
    return best;
}

// Не даёт оптимизатору выбросить вычисление, результат которого не
// используется. Запись в volatile — наблюдаемый эффект.
template <class T>
void keep(const T& value) {
    static volatile std::size_t sink = 0;
    sink = sink + static_cast<std::size_t>(std::hash<T>{}(value));
}

}  // namespace kvs::bench
