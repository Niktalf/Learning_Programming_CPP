// Не должно собираться. Проверяется тестом taskq_dangling_range_fail.
//
// queues_snapshot возвращает вектор по значению — временный объект. Алгоритм
// ranges::find над временным контейнером возвращает не итератор, а
// std::ranges::dangling: итератор указывал бы в вектор, разрушенный в конце
// полного выражения. Разыменовать dangling нельзя — компилятор не даёт
// сделать висячую ссылку, как удалённая перегрузка tokenize на этапе 2.
//
// Защита работает, только пока тип не объявлен borrowed_range. string_view
// и span — заимствованные: итератор в них переживает сам объект-вид, и
// ranges::find над временным string_view вернёт обычный итератор. Это
// корректно — данные принадлежат не виду, — и показывает, что защита
// держится на объявлении, а не на анализе времени жизни.

#include <algorithm>
#include <string>
#include <vector>

namespace {

std::vector<std::string> queues_snapshot() {
    return {"build", "deploy", "test"};
}

}  // namespace

int main() {
    const auto found = std::ranges::find(queues_snapshot(), "deploy");
    return found->size() == 6 ? 0 : 1;   // ошибка: у std::ranges::dangling нет operator->
}
