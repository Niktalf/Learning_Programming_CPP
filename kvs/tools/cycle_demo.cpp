// Звёздочка этапа 4: цикл из shared_ptr.
//
//   cmake --build build --target kvs_cycle_demo kvs_cycle_leak
//   ./build/kvs_cycle_demo     обратная ссылка — weak_ptr, всё освобождается
//   ./build/kvs_cycle_leak     обратная ссылка — shared_ptr, утечка
//
// kvs_cycle_leak печатает «alive objects: 2» после ухода клиента: счётчик
// живых объектов показывает утечку без всякого санитайзера. В сборке с
// санитайзером адресов программа завершается так (вывод LeakSanitizer,
// кадры стека сокращены до первого):
//
//   ==ERROR: LeakSanitizer: detected memory leaks
//   Indirect leak of 72 byte(s) in 1 object(s) allocated from:
//       ... std::_Sp_counted_ptr_inplace<kvs::cycle::Client<std::shared_ptr>, ...>
//   Indirect leak of 64 byte(s) in 1 object(s) allocated from:
//       ... std::_Sp_counted_ptr_inplace<kvs::cycle::Subscription<std::shared_ptr>, ...>
//   Indirect leak of 16 byte(s) in 1 object(s) allocated from:
//       ... std::shared_ptr<kvs::cycle::Subscription<std::shared_ptr>> (буфер вектора)
//   SUMMARY: AddressSanitizer: 152 byte(s) leaked in 3 allocation(s).
//
// Все три утечки — косвенные, ни одной прямой. Это и есть почерк цикла:
// прямая утечка — блок, на который не указывает ничто, а здесь каждый блок
// указан другим утёкшим блоком. _Sp_counted_ptr_inplace — единый блок
// make_shared: объект и контрольный блок вместе.

#include <iostream>
#include <memory>

#include "cycle.hpp"

namespace {

// Вид обратной ссылки выбирается флагом компиляции. Псевдоним — на весь
// класс клиента, а не на shared_ptr или weak_ptr: псевдоним шаблона,
// переданный как шаблонный параметр, не все компиляторы считают тем же самым
// шаблоном, и перегрузка owner_name перестала бы находиться.
#ifdef KVS_LEAKING_CYCLE
using Client = kvs::cycle::Client<std::shared_ptr>;
constexpr const char* variant = "back reference is shared_ptr (leaks)";
#else
using Client = kvs::cycle::Client<std::weak_ptr>;
constexpr const char* variant = "back reference is weak_ptr";
#endif

}  // namespace

int main() {
    using kvs::cycle::alive;
    std::cout << variant << '\n';
    {
        const auto client = std::make_shared<Client>("alice");
        kvs::cycle::subscribe(client, "counter");
        std::cout << "subscribed: " << kvs::cycle::owner_name(*client->subscriptions.front())
                  << " watches " << client->subscriptions.front()->key << '\n'
                  << "client use_count: " << client.use_count() << '\n'
                  << "alive objects:    " << alive() << '\n';
    }
    // Последняя внешняя ссылка на клиента ушла вместе с блоком.
    // flush явный: при утечке LeakSanitizer завершает процесс сам, и
    // невыведенный буфер потерялся бы.
    std::cout << "after the client is gone, alive objects: " << alive() << std::endl;
    return 0;
}
