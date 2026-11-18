// Этап 7, пункт 2: три представления истории комнаты на профиле чата.
//
//   cmake -B build-release -DCMAKE_BUILD_TYPE=Release
//   cmake --build build-release --target chat_bench_history
//   ./build-release/chat_bench_history                 # таблица времени и выделений
//   valgrind --tool=cachegrind --cache-sim=yes ./build-release/chat_bench_history deque 1000
//                                                      # промахи кэша, один вариант
//
// Профиль: история ограничена ёмкостью C. Миллион сообщений подряд, каждое —
// в конец; когда история полна, самое старое вытесняется. После каждого
// десятого сообщения — чтение двадцати последних, как у /history по
// умолчанию. Элемент — 8-байтное число: так выглядит MessagePtr, указатель
// на сообщение; само сообщение выделяется одинаково при любом контейнере, и
// меряется только контейнер.
//
// Результаты и выводы — в docs/benchmarks.md.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <iterator>
#include <list>
#include <string>
#include <string_view>
#include <vector>

#include "bench_util.hpp"

namespace {

using Item = std::uint64_t;

// Кольцо на vector: место выделяется один раз, дальше новое сообщение
// перезаписывает самое старое, и «начало» сдвигается по кругу.
//
// Два варианта, которые различаются одним: как индекс переходит через конец.
// Modulo = true — через остаток от деления, как пишут в учебнике; false —
// сравнением и вычитанием. Деление — одна из самых медленных целых
// операций, десятки тактов, и в горячем цикле это видно (docs/benchmarks.md).
template <bool Modulo>
class VectorRing {
public:
    explicit VectorRing(std::size_t capacity) : capacity_(capacity) { items_.reserve(capacity); }

    void push(Item item) {
        if (items_.size() < capacity_) {
            items_.push_back(item);
            return;
        }
        items_[head_] = item;
        if constexpr (Modulo) {
            head_ = (head_ + 1) % capacity_;
        } else {
            head_ = head_ + 1 == capacity_ ? 0 : head_ + 1;
        }
    }

    // Сумма count последних — чтение «последних N».
    [[nodiscard]] Item last(std::size_t count) const {
        const std::size_t size = items_.size();
        Item sum = 0;
        for (std::size_t i = size - std::min(count, size); i < size; ++i) {
            if constexpr (Modulo) {
                sum += items_[(head_ + i) % size];
            } else {
                const std::size_t at = head_ + i;
                sum += items_[at < size ? at : at - size];
            }
        }
        return sum;
    }

private:
    std::vector<Item> items_;
    std::size_t capacity_;
    std::size_t head_ = 0;   // индекс самого старого, когда кольцо полно
};

// deque и list: push_back и pop_front — вытеснение буквально.
template <class Container>
class Queue {
public:
    explicit Queue(std::size_t capacity) : capacity_(capacity) {}

    void push(Item item) {
        if (items_.size() == capacity_) {
            items_.pop_front();
        }
        items_.push_back(item);
    }

    [[nodiscard]] Item last(std::size_t count) const {
        Item sum = 0;
        auto it = items_.end();
        for (std::size_t i = 0; i < count && it != items_.begin(); ++i) {
            sum += *--it;
        }
        return sum;
    }

private:
    Container items_;
    std::size_t capacity_;
};

constexpr std::size_t messages = 1000000;
constexpr std::size_t read_last = 20;

template <class History>
void run(std::size_t capacity) {
    History history(capacity);
    Item sum = 0;
    for (std::size_t i = 0; i < messages; ++i) {
        history.push(i);
        if (i % 10 == 9) {
            sum += history.last(read_last);
        }
    }
    chat::bench::keep(sum);
}

template <class History>
void report(const char* name, std::size_t capacity) {
    const auto result = chat::bench::measure(
        5, [] { return 0; }, [&](int) { run<History>(capacity); });
    std::cout << "| " << name << " | " << capacity << " | " << result.milliseconds << " | "
              << result.allocations << " |\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 3) {
        const std::string_view which = argv[1];
        const auto capacity = static_cast<std::size_t>(std::stoul(argv[2]));
        if (which == "ring") {
            run<VectorRing<false>>(capacity);
        } else if (which == "ring-modulo") {
            run<VectorRing<true>>(capacity);
        } else if (which == "deque") {
            run<Queue<std::deque<Item>>>(capacity);
        } else if (which == "list") {
            run<Queue<std::list<Item>>>(capacity);
        } else {
            std::cerr << "usage: chat_bench_history [ring|ring-modulo|deque|list CAPACITY]\n";
            return 2;
        }
        return 0;
    }
    std::cout << "10^6 messages, read the last 20 after every 10th\n\n"
              << "| history | capacity | best, ms | allocations |\n|---|---|---|---|\n";
    for (const std::size_t capacity : {100u, 1000u, 10000u}) {
        report<VectorRing<true>>("vector ring, %", capacity);
        report<VectorRing<false>>("vector ring", capacity);
        report<Queue<std::deque<Item>>>("deque", capacity);
        report<Queue<std::list<Item>>>("list", capacity);
    }
    return 0;
}
