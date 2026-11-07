#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace chat {

// Владелец входной строки. Результат разбора смотрит внутрь неё, поэтому
// строка обязана пережить команду.
//
// Перемещение запрещено намеренно. Причина тоньше, чем кажется: короткая
// строка живёт внутри самого объекта std::string (оптимизация коротких строк),
// и её перемещение переносит символы в новый адрес. Все ранее полученные
// представления после этого указывают в никуда — причём только для коротких
// строк, то есть ошибка проявляется выборочно и выглядит мистикой.
//
// На этапе 15 строка будет приходить из сокета, и цена такой ошибки вырастет
// многократно: представление переживёт буфер приёма, а не локальную строку.
//
// С этапа 6 разбор делает реестр команд, а не parse, но правило то же:
// аргументы связанной команды — string_view внутрь этой строки.
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

}  // namespace chat
