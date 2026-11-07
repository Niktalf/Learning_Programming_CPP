#pragma once

#include <string_view>

#include "kvs/registry.hpp"
#include "kvs/store.hpp"

namespace kvs {

// Этап 6: все команды хранилища — в одном реестре. Добавить команду — одна
// строка регистрации в src/commands.cpp и функция-обработчик рядом с ней.
[[nodiscard]] const CommandRegistry<Store>& commands();

// Предел KEYS по умолчанию: таблица может быть огромной, и выдавать её
// целиком в ответ на опечатку в образце — плохая идея.
inline constexpr std::size_t default_keys_limit = 100;

// Разобрать и сразу исполнить одну строку: лексер, затем реестр. Строка
// живёт всё время вызова, поэтому временная здесь безопасна. Используют
// восстановление из журнала и тесты; сервер идёт через кэш запросов.
[[nodiscard]] Response execute(Store& store, std::string_view line);

}  // namespace kvs
