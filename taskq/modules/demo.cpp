// Потребитель модуля taskq.state. Заголовки и import в одной единице
// трансляции: сначала #include, потом import — обратный порядок с GCC 15 не
// собирается, см. docs/modules.md.

#include <print>

import taskq.state;

using taskq::modules::State;

// Проверка при компиляции — через границу модуля.
static_assert(taskq::modules::allowed(State::Leased, State::Pending));

int main() {
    constexpr State all[] = {State::Pending, State::Leased, State::Done, State::Failed,
                             State::Cancelled};
    for (const State from : all) {
        for (const State to : all) {
            if (taskq::modules::allowed(from, to)) {
                std::println("{} -> {}", taskq::modules::name(from), taskq::modules::name(to));
            }
        }
    }
    return 0;
}
