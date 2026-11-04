// Звёздочка этапа 4: цикл из shared_ptr.
//
//   cmake --build build --target taskq_cycle_demo taskq_cycle_leak
//   ./build/taskq_cycle_demo    задача держит воркера через weak_ptr
//   ./build/taskq_cycle_leak    через shared_ptr — утечка
//
// taskq_cycle_leak печатает «alive objects: 2» после того, как ушли и воркер,
// и задача: счётчик живых объектов показывает утечку без всякого санитайзера.
// Под санитайзером адресов программа завершается отчётом LeakSanitizer —
// «detected memory leaks», и все блоки помечены как косвенные утечки
// (Indirect leak). Это почерк цикла: прямая утечка — блок, на который не
// указывает ничто, а здесь каждый блок указан другим утёкшим.
//
// Цикл опасен здесь вдвойне: задача, удерживающая воркера, никогда не станет
// брошенной, и механизм возврата задач от исчезнувших воркеров, ради
// которого аренда хранит держателя, перестанет работать.

#include <iostream>
#include <memory>

#include "cycle.hpp"

namespace {

// Вид ссылки выбирается флагом компиляции. Псевдонимы — на целые классы, а
// не на shared_ptr или weak_ptr: псевдоним шаблона, переданный как шаблонный
// параметр, не все компиляторы считают тем же самым шаблоном.
#ifdef TASKQ_LEAKING_CYCLE
using Worker = taskq::cycle::Worker<std::shared_ptr>;
using Job = taskq::cycle::Job<std::shared_ptr>;
constexpr const char* variant = "job holds its worker by shared_ptr (leaks)";
#else
using Worker = taskq::cycle::Worker<std::weak_ptr>;
using Job = taskq::cycle::Job<std::weak_ptr>;
constexpr const char* variant = "job holds its worker by weak_ptr";
#endif

}  // namespace

int main() {
    using taskq::cycle::alive;
    std::cout << variant << '\n';
    {
        const auto worker = std::make_shared<Worker>("w1");
        const auto job = std::make_shared<Job>("make all");
        taskq::cycle::lease(worker, job);
        std::cout << "worker use_count: " << worker.use_count() << '\n'
                  << "job use_count:    " << job.use_count() << '\n';
    }
    // flush явный: при утечке LeakSanitizer завершает процесс сам, и
    // невыведенный буфер потерялся бы.
    std::cout << "after both are gone, alive objects: " << alive() << std::endl;
    return 0;
}
