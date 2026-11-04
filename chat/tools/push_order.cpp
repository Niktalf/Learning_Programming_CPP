// Звёздочка этапа 1: порядок вызовов конструкторов и присваиваний при
// добавлении в std::vector временного объекта, переменной и результата
// std::move, а также что происходит при перевыделении вектора.
//
//   cmake --build build --target chat_push_order && ./build/chat_push_order
//
// Что напечатается и откуда берётся каждая операция:
//
// 1. push_back(Traced("x")) — временный объект.
//    construct — сам временный объект; move — переезд в буфер вектора.
//
// 2. push_back(variable) — переменная.
//    copy — переменная продолжает жить, забрать её ресурсы нельзя.
//
// 3. push_back(std::move(variable)) — результат std::move.
//    move — и всё: копию убирает std::move, а не emplace_back.
//
// 4. emplace_back("e") — объект строится прямо в буфере.
//    construct — перемещение из случая 1 исчезает.
//
// 5. Перевыделение. Вектору ёмкостью 2 добавляется третий элемент: он
//    строится в новом буфере (construct), затем два старых переезжают туда же.
//    Переезжают они через std::move_if_noexcept: если перемещающий конструктор
//    помечен noexcept — move, move; если нет — copy, copy.
//
//    Почему так. push_back даёт сильную гарантию: если операция бросила,
//    вектор остаётся прежним. Копирование старых элементов этому не мешает —
//    оригиналы целы, новый буфер можно просто выбросить. А перемещение,
//    способное бросить на середине, оставило бы часть элементов уже
//    выпотрошенными в старом буфере, и вернуть прежнее состояние было бы
//    нечем. Поэтому без noexcept вектор выбирает копирование — и при каждом
//    перевыделении копирует все накопленные элементы.

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "traced.hpp"

namespace {

using chat::testing::Traced;
using chat::testing::TracedThrowingMove;
using chat::testing::trace_log;

void report(const std::string& title) {
    std::cout << title << ":";
    for (const std::string& event : trace_log()) {
        std::cout << ' ' << event;
    }
    std::cout << '\n';
    trace_log().clear();
}

template <class T>
void reallocate(const std::string& title) {
    std::vector<T> items;
    items.reserve(2);
    items.emplace_back("a");
    items.emplace_back("b");
    trace_log().clear();
    items.emplace_back("c");   // ёмкость исчерпана: перевыделение
    report(title);
}

}  // namespace

int main() {
    std::vector<Traced> items;
    items.reserve(8);   // чтобы случаи 1–4 не смешивались с перевыделением

    trace_log().clear();
    items.push_back(Traced("x"));
    report("1. temporary              ");

    Traced variable("y");
    trace_log().clear();
    items.push_back(variable);
    report("2. variable               ");

    items.push_back(std::move(variable));
    report("3. std::move              ");

    items.emplace_back("e");
    report("4. emplace_back           ");

    reallocate<Traced>("5. reallocation, noexcept ");
    reallocate<TracedThrowingMove>("5. reallocation, may throw");

    return 0;
}
