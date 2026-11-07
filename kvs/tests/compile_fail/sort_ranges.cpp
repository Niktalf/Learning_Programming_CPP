// Не должно собираться. Проверяется тестом kvs_sort_ranges_fail.
//
// Та же ошибка с std::ranges::sort. Его требования записаны концепциями —
// random_access_range, sortable, — и компилятор отказывает на входе в
// алгоритм, называя невыполненное требование, вместо того чтобы проваливаться
// в его реализацию.

#include <algorithm>
#include <list>
#include <string>

int main() {
    std::list<std::string> keys = {"b", "a", "c"};
    std::ranges::sort(keys);
    return 0;
}
