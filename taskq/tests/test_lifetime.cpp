#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>

#include "taskq/lexer.hpp"
#include "taskq/input_line.hpp"

// Дракон этапа: висячее представление.
//
// Ошибка, которую мы закрываем, выглядит безобидно:
//
//     const auto tokens = tokenize(std::string("SUBMIT build 5 x"));
//     use(tokens[1]);        // временная строка уже разрушена
//
// Вызов не компилируется: перегрузка для временной строки удалена. Это
// архитектурное исправление — ошибка стала невозможной по
// построению, а не «мы договорились так не писать».
//
// Чтобы увидеть, чем всё кончается без этой защиты, соберите цель
// taskq_dangling_demo и запустите её под санитайзером адресов. Она намеренно
// воспроизводит порчу памяти и в обычную сборку не входит.

namespace {

template <class T, class = void>
struct tokenize_accepts : std::false_type {};

template <class T>
struct tokenize_accepts<T, std::void_t<decltype(taskq::tokenize(std::declval<T>()))>>
    : std::true_type {};

}  // namespace

TEST(Lifetime, TokenizeRejectsTemporaryString) {
    EXPECT_TRUE(tokenize_accepts<std::string_view>::value);
    EXPECT_FALSE(tokenize_accepts<std::string&&>::value);
}

TEST(Lifetime, InputLineIsNeitherCopyableNorMovable) {
    // Перемещение запрещено не из вредности: короткая строка живёт внутри
    // самого объекта (оптимизация коротких строк), и перемещение переносит
    // символы в новый адрес. Представления после этого висят — причём только
    // для коротких строк, то есть выборочно и непредсказуемо.
    EXPECT_FALSE(std::is_copy_constructible_v<taskq::InputLine>);
    EXPECT_FALSE(std::is_move_constructible_v<taskq::InputLine>);
}
