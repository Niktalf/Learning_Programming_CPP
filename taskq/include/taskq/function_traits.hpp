#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace taskq {

// Этап 6: сигнатура вызываемого объекта во время компиляции — тип результата,
// число и типы параметров. Частичные специализации разбирают тип функции на
// части; лямбда разбирается через тип указателя на её operator().
//
// Основной шаблон пуст. Для типа, который разобрать нельзя, — обобщённой
// лямбды с параметром auto, объекта с несколькими operator(), — у
// function_traits просто нет членов, и это видно концепции: проверка
// typename function_traits<F>::result тихо не проходит, вместо жёсткой
// ошибки где-то в глубине шаблона.
template <class F>
struct function_traits {};

template <class R, class... Args>
struct function_traits<R (*)(Args...)> {
    using object = void;   // свободная функция: объекта нет
    using result = R;
    using arguments = std::tuple<Args...>;
    static constexpr std::size_t arity = sizeof...(Args);

    template <std::size_t I>
    using arg = std::tuple_element_t<I, arguments>;
};

// С C++17 noexcept — часть типа функции: R(*)(Args...) noexcept — другой
// тип, и без своей специализации он не разобрался бы.
template <class R, class... Args>
struct function_traits<R (*)(Args...) noexcept> : function_traits<R (*)(Args...)> {};

template <class R, class... Args>
struct function_traits<R(Args...)> : function_traits<R (*)(Args...)> {};

// Указатель на метод. Оператор вызова лямбды — тоже метод: константный по
// умолчанию, неконстантный у mutable, и у каждого — вариант с noexcept.
//
// В варианте TaskQ обработчики команд — методы Service, поэтому у
// указателя на метод есть ещё и object: класс, к которому метод относится, с
// const для константного метода. У свободной функции и лямбды object —
// void: их вызывают без объекта.
template <class C, class R, class... Args>
struct function_traits<R (C::*)(Args...) const> : function_traits<R (*)(Args...)> {
    using object = const C;
};

template <class C, class R, class... Args>
struct function_traits<R (C::*)(Args...) const noexcept> : function_traits<R (*)(Args...)> {
    using object = const C;
};

template <class C, class R, class... Args>
struct function_traits<R (C::*)(Args...)> : function_traits<R (*)(Args...)> {
    using object = C;
};

template <class C, class R, class... Args>
struct function_traits<R (C::*)(Args...) noexcept> : function_traits<R (*)(Args...)> {
    using object = C;
};

// Объект с единственным, не шаблонным operator() — лямбда или функтор.
// Ограничение requires делает специализацию «мягкой»: у обобщённой лямбды
// &F::operator() не берётся, специализация не подходит, и остаётся пустой
// основной шаблон.
template <class F>
    requires requires { &F::operator(); }
struct function_traits<F> : function_traits<decltype(&F::operator())> {
    // Лямбда вызывается как свободная функция: объекта у неё в сигнатуре нет.
    using object = void;
};

}  // namespace taskq
