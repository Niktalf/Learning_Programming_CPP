// Звёздочка этапа 4: цикл из shared_ptr.
//
//   cmake --build build --target chat_cycle_demo chat_cycle_leak
//   ./build/chat_cycle_demo    комната держит участников через weak_ptr
//   ./build/chat_cycle_leak    через shared_ptr — утечка
//
// chat_cycle_leak печатает «alive objects: 2» после того, как ушли и
// пользователь, и комната: счётчик живых объектов показывает утечку без
// всякого санитайзера. Под санитайзером адресов программа завершается
// отчётом LeakSanitizer — «detected memory leaks», и все блоки помечены как
// косвенные утечки (Indirect leak). Это почерк цикла: прямая утечка — блок,
// на который не указывает ничто, а здесь каждый блок указан другим утёкшим.

#include <iostream>
#include <memory>

#include "cycle.hpp"

namespace {

// Вид ссылки выбирается флагом компиляции. Псевдонимы — на целые классы, а
// не на shared_ptr или weak_ptr: псевдоним шаблона, переданный как шаблонный
// параметр, не все компиляторы считают тем же самым шаблоном.
#ifdef CHAT_LEAKING_CYCLE
using User = chat::cycle::User<std::shared_ptr>;
using Room = chat::cycle::Room<std::shared_ptr>;
constexpr const char* variant = "room holds members by shared_ptr (leaks)";
#else
using User = chat::cycle::User<std::weak_ptr>;
using Room = chat::cycle::Room<std::weak_ptr>;
constexpr const char* variant = "room holds members by weak_ptr";
#endif

}  // namespace

int main() {
    using chat::cycle::alive;
    std::cout << variant << '\n';
    {
        const auto alice = std::make_shared<User>("alice");
        const auto general = std::make_shared<Room>("#general");
        chat::cycle::join(alice, general);
        std::cout << "members of " << general->name << ": "
                  << chat::cycle::alive_members(*general) << '\n'
                  << "alice use_count:   " << alice.use_count() << '\n'
                  << "general use_count: " << general.use_count() << '\n';
    }
    // flush явный: при утечке LeakSanitizer завершает процесс сам, и
    // невыведенный буфер потерялся бы.
    std::cout << "after both are gone, alive objects: " << alive() << std::endl;
    return 0;
}
