// Не должно собираться. Проверяется тестом kvs_enable_if_diagnostics_fail.
//
// Та же проверка ключа, что и KeyLike, но записанная так, как это делали до
// C++20: std::enable_if в типе возвращаемого значения. Ошибка та же —
// ключ-число, — а диагностика сообщает лишь, что подходящей функции нет и у
// кандидата «нет типа type в enable_if». Какое именно условие не выполнено,
// приходится выяснять, читая условие самому.

#include <string>
#include <string_view>
#include <type_traits>

namespace {

template <class T>
struct is_key : std::bool_constant<std::is_same_v<std::remove_cvref_t<T>, std::string> ||
                                   std::is_same_v<std::remove_cvref_t<T>, std::string_view>> {};

template <class K>
std::enable_if_t<is_key<K>::value, bool> contains(const K& key) {
    return !std::string_view(key).empty();
}

}  // namespace

int main() {
    return contains(42) ? 0 : 1;
}
