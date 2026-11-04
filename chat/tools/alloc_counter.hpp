#pragma once

#include <cstddef>

namespace chat::tools {

// Счётчик вызовов глобального operator new. Сама замена operator new живёт в
// alloc_counter.cpp и подключается только к тем программам, которым нужен
// подсчёт: подменить operator new можно один раз на всю программу.
[[nodiscard]] std::size_t allocations() noexcept;
void reset_allocations() noexcept;

// Под санитайзером адресов подмена отключена: санитайзер сам подменяет
// operator new и следит, чтобы память, выделенную им, освобождал он же.
// Наша подмена ломала бы эту проверку, поэтому в такой сборке счётчик
// не работает, а тесты подсчёта пропускаются.
[[nodiscard]] bool counting_available() noexcept;

}  // namespace chat::tools
