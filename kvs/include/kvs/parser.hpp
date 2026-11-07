#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "kvs/command.hpp"
#include "kvs/errors.hpp"
#include <expected>

namespace kvs {

// Владелец входной строки. Результат разбора смотрит внутрь неё, поэтому
// строка обязана пережить команду. Перемещение запрещено: короткая строка
// живёт внутри самого std::string, и перемещение переносит символы в новый
// адрес, оставляя представления висячими.
class InputLine {
public:
    explicit InputLine(std::string text) noexcept : text_(std::move(text)) {}

    InputLine(const InputLine&) = delete;
    InputLine& operator=(const InputLine&) = delete;
    InputLine(InputLine&&) = delete;
    InputLine& operator=(InputLine&&) = delete;

    [[nodiscard]] std::string_view view() const noexcept { return text_; }

private:
    std::string text_;
};

// Этап 3: пустой optional этапа 2 заменён на исключение ParseError с
// причиной отказа — тем, чего не хватало пустому optional.
[[nodiscard]] Command parse(const InputLine& line);

// Второй путь: та же причина отказа, но возвращённая значением. Обе версии
// оставлены в коде; сравнение — в docs/errors.md.
[[nodiscard]] std::expected<Command, ParseError> try_parse(const InputLine& line);

// Перегрузки для временного объекта удалены: результат пережил бы аргумент.
Command parse(InputLine&&) = delete;
std::expected<Command, ParseError> try_parse(InputLine&&) = delete;

}  // namespace kvs
