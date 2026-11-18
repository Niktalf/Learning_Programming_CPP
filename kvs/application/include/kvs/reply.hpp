#pragma once

#include <concepts>
#include <exception>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "kvs/registry.hpp"

namespace kvs {

// Домашнее задание этапа 9: Pure Fabrication (GRASP).
//
// Как выглядит ответ клиенту — «OK», число, строки через перевод строки,
// «(empty)» для пустого списка, «ERR причина (контекст)» для ошибки, — не
// обязанность ни одной сущности предметной области. Хранилище о формате
// ответа знать не должно, реестр команд — тоже: он разбирает аргументы, а не
// печатает. До этапа 9 формат жил в двух местах: в commands.cpp —
// функция ok() и четыре копии «пусто — (empty), иначе join_with», — и в
// server.cpp — reason_of и склейка цепочки контекста (DRY и SRP,
// docs/audit.md, записи 5 и 7).
//
// Reply — придуманный класс: в домене его нет, он появился, чтобы снять эту
// обязанность с тех, кому она не принадлежит. На этапе 15 другой формат —
// протокол RESP вместо строк — меняет этот класс и только его.
class Reply {
public:
    [[nodiscard]] static Response ok(std::string text = "OK") { return Response{true, std::move(text)}; }

    // Строки по одной на линию; пустой диапазон — «(empty)».
    template <std::ranges::forward_range R>
        requires std::convertible_to<std::ranges::range_reference_t<const R>, std::string_view>
    [[nodiscard]] static Response lines(const R& items) {
        if (std::ranges::empty(items)) {
            return ok(std::string(empty_list));
        }
        return ok(std::ranges::to<std::string>(items | std::views::join_with('\n')));
    }

    // Ответ на исключение: «ERR <исходная причина> (<звено контекста>)...».
    // Клиенту видно и что случилось, и на какой команде.
    [[nodiscard]] static Response error(const std::exception_ptr& error);

    static constexpr std::string_view empty_list = "(empty)";
    static constexpr std::string_view nil = "(nil)";
};

}  // namespace kvs
