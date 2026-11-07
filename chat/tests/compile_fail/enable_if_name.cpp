// Не должно собираться. Проверяется тестом chat_enable_if_diagnostics_fail.
//
// Та же проверка имени, что и NameLike, но записанная так, как это делали до
// C++20: std::enable_if в типе возвращаемого значения. Ошибка та же, а
// диагностика сообщает лишь, что у кандидата «нет типа type в enable_if».
// Какое именно условие не выполнено, приходится выяснять самому.

#include <string>
#include <string_view>
#include <type_traits>

namespace {

template <class T>
struct is_name : std::bool_constant<std::is_same_v<std::remove_cvref_t<T>, std::string> ||
                                    std::is_same_v<std::remove_cvref_t<T>, std::string_view>> {};

template <class N>
std::enable_if_t<is_name<N>::value, bool> contains(const N& room) {
    return !std::string_view(room).empty();
}

}  // namespace

int main() {
    return contains(42) ? 0 : 1;
}
