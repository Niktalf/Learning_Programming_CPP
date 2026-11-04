// Звёздочка этапа 1: порядок вызовов конструкторов и присваиваний при вставке
// в std::map временного объекта, объекта-переменной и результата std::move.
//
//   cmake --build build --target kvs_insert_order && ./build/kvs_insert_order
//
// Что напечатается и откуда берётся каждая операция (libstdc++, C++14):
//
// 1. insert(std::make_pair(key, Traced("x"))) — временный объект.
//    construct — сам временный Traced;
//    move      — make_pair перемещает его внутрь пары;
//    move      — insert(P&&) строит узел дерева из пары-временного.
//    Копий нет, но два перемещения лишние: объект мог сразу родиться в узле.
//
// 2. insert(std::make_pair(key, variable)) — именованная переменная.
//    copy      — make_pair копирует lvalue в пару: переменная продолжает жить,
//                забрать её ресурсы нельзя;
//    move      — пара переезжает в узел.
//    Это единственная настоящая копия во всех трёх случаях, и её устраняет
//    не emplace, а только std::move — если переменная больше не нужна.
//
// 3. insert(std::make_pair(key, std::move(variable))) — результат std::move.
//    move, move — как в первом случае, но без construct: объект уже был.
//
// 4. emplace(std::piecewise_construct, ...) — объект строится прямо в узле.
//    construct — и больше ничего. Именно эти два перемещения из случаев 1 и 3
//    устраняет emplace. Копию из случая 2 он не устранит: emplace(key, variable)
//    всё равно скопирует переменную в узел.
//
// 5. map[key] = Traced("w") — для сравнения.
//    construct — временный объект справа;
//    default   — operator[] сначала создаёт пустое значение в узле;
//    move-assign — затем в него присваивается временный объект.
//    Лишнее конструирование по умолчанию — цена operator[]; поэтому Store::set
//    использует find и insert, а не operator[].

#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "traced.hpp"

namespace {

using kvs::testing::Traced;
using kvs::testing::trace_log;

void report(const std::string& title) {
    std::cout << title << ":";
    for (const std::string& event : trace_log()) {
        std::cout << ' ' << event;
    }
    std::cout << '\n';
    trace_log().clear();
}

}  // namespace

int main() {
    std::map<std::string, Traced> map;

    trace_log().clear();
    map.insert(std::make_pair(std::string("temporary"), Traced("x")));
    report("1. temporary      ");

    Traced variable("y");
    trace_log().clear();
    map.insert(std::make_pair(std::string("variable"), variable));
    report("2. variable       ");

    Traced movable("z");
    trace_log().clear();
    map.insert(std::make_pair(std::string("moved"), std::move(movable)));
    report("3. std::move      ");

    map.emplace(std::piecewise_construct, std::forward_as_tuple("emplaced"),
                std::forward_as_tuple("e"));
    report("4. emplace        ");

    map["subscript"] = Traced("w");
    report("5. operator[]     ");

    return 0;
}
