#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "taskq/command.hpp"

namespace taskq {

// Владелец входной строки. Результат разбора смотрит внутрь неё, поэтому
// строка обязана пережить команду.
//
// Перемещение запрещено намеренно. Причина тоньше, чем кажется: короткая
// строка живёт внутри самого объекта std::string (оптимизация коротких строк),
// и её перемещение переносит символы в новый адрес. Все ранее полученные
// представления после этого указывают в никуда — причём только для коротких
// строк, то есть ошибка проявляется выборочно и выглядит мистикой.
//
// В этом проекте цена ошибки особенно высока: тело задачи переживает
// соединение, по которому пришло, и на этапе 15 будет отдано другому клиенту.
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

// Пустой результат означает «строка не разобрана». На этапе 3 он сменится
// иерархией исключений, и станет видно, чего здесь не хватает: причины отказа.
[[nodiscard]] std::optional<Command> parse(const InputLine& line);

// Перегрузка для временного объекта удалена: результат разбора пережил бы
// аргумент. Тот же приём, что и с tokenize, — ошибка закрывается интерфейсом,
// а не дисциплиной вызывающего.
std::optional<Command> parse(InputLine&&) = delete;

}  // namespace taskq
