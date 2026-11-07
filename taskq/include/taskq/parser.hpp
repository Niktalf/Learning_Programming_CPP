#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "taskq/command.hpp"
#include "taskq/errors.hpp"
#include <expected>

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

// Этап 3: два пути разбора.
//
// parse бросает ParseError с причиной отказа и исходной строкой — то, чего
// не хватало пустому std::optional на этапе 2. Этим путём пользуется граница
// handle.
//
// try_parse возвращает ту же ошибку значением. Это второй путь, ради
// сравнения: на одних и тех же входах оба дают одну и ту же причину, а чем
// они различаются по читаемости, стоимости и пробросу через уровни, написано
// в docs/errors.md.
[[nodiscard]] Command parse(const InputLine& line);
[[nodiscard]] std::expected<Command, ParseError> try_parse(const InputLine& line);

// Перегрузки для временного объекта удалены: результат разбора пережил бы
// аргумент. Тот же приём, что и с tokenize, — ошибка закрывается интерфейсом,
// а не дисциплиной вызывающего.
Command parse(InputLine&&) = delete;
std::expected<Command, ParseError> try_parse(InputLine&&) = delete;

}  // namespace taskq
