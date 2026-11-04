#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace chat {

// Проверки ввода, которые нашёл фаззер из домашнего задания этапа 3. Каждая
// закрыта тестом в tests/test_validate.cpp; что именно пропускал разбор до
// них, написано в tests/test_fuzz.cpp.
//
// Функции возвращают причину отказа, а не бросают: ими пользуются оба пути
// разбора — и на исключениях, и на Result, — и путь сам решает, как
// доставить ошибку.

// Строка длиннее этого предела отвергается целиком, до разбора. Без предела
// строка в мегабайт честно ложилась в историю и раздувала каждый /history.
inline constexpr std::size_t max_line_length = 4096;

// Имя комнаты и ник: не длиннее 32 символов.
inline constexpr std::size_t max_name_length = 32;

// Строгая проверка UTF-8: обрезанные последовательности, лишние байты
// продолжения, избыточные (overlong) формы и суррогаты — ошибка.
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;

// Имя комнаты: '#', затем от 1 до 32 латинских букв, цифр, '_' или '-'.
[[nodiscard]] std::optional<std::string> room_name_problem(std::string_view room);

// Ник: от 1 до 32 латинских букв, цифр, '_' или '-'.
[[nodiscard]] std::optional<std::string> nick_problem(std::string_view nick);

// Текст сообщения или темы: непустой, без управляющих символов. Управляющие
// символы опасны не для сервера, а для других участников: ESC-последовательность
// в сообщении перекрасила бы или очистила чужой терминал.
[[nodiscard]] std::optional<std::string> text_problem(std::string_view text);

}  // namespace chat
