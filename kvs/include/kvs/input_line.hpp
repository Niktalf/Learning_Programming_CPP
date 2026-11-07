#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace kvs {

// Владелец входной строки. Результат разбора смотрит внутрь неё, поэтому
// строка обязана пережить команду. Перемещение запрещено: короткая строка
// живёт внутри самого std::string, и перемещение переносит символы в новый
// адрес, оставляя представления висячими.
//
// С этапа 6 разбор делает реестр команд, а не parse, но правило то же:
// аргументы разобранной команды — string_view внутрь этой строки.
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

}  // namespace kvs
