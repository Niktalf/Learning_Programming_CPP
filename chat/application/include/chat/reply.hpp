#pragma once

#include <concepts>
#include <exception>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/registry.hpp"

namespace chat {

// Домашнее задание этапа 9: Pure Fabrication (GRASP).
//
// Как сообщение и ответ выглядят для клиента — «номер: <автор> текст»,
// «номер @ms: <автор> текст», строки через перевод строки, «(empty)»,
// «ERR причина (контекст)», — не обязанность ни одной сущности предметной
// области. Сообщение знает своё содержимое, но не то, как его печатать в
// конкретном протоколе; хранилище — тем более; сессии на этапе 15 нужно
// только отправить готовые байты. До этапа 9 формат был размазан: функции
// ok, joined и lines_of в commands.cpp и reason_of со склейкой контекста в
// server.cpp (docs/audit.md, записи 5 и 6).
//
// Reply — придуманный класс: в домене чата его нет. На этапе 15 протокол
// IRC (строки вида «:alice!alice@host PRIVMSG #general :hi») поменяет этот
// класс и только его.
class Reply {
public:
    [[nodiscard]] static Response ok(std::string text = "OK") { return Response{true, std::move(text)}; }

    // Строки по одной на линию; пусто — empty.
    template <std::ranges::forward_range R>
        requires std::convertible_to<std::ranges::range_reference_t<const R>, std::string_view>
    [[nodiscard]] static Response lines(const R& items, std::string_view empty = empty_list) {
        if (std::ranges::empty(items)) {
            return ok(std::string(empty));
        }
        return ok(std::ranges::to<std::string>(items | std::views::join_with('\n')));
    }

    // Сообщение истории для клиента: «номер: <автор> текст», с отметкой
    // времени — «номер @ms: <автор> текст».
    [[nodiscard]] static std::string message_line(const ChatStore::Entry& entry, bool with_time);

    // Страница истории — строки сообщений или «(empty)».
    [[nodiscard]] static Response history(const std::vector<ChatStore::Entry>& entries, bool with_time);

    // Ответ на исключение: «ERR <исходная причина> (<звено контекста>)...».
    [[nodiscard]] static Response error(const std::exception_ptr& error);

    static constexpr std::string_view empty_list = "(empty)";
};

}  // namespace chat
