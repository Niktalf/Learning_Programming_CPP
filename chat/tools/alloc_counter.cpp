#include "alloc_counter.hpp"

#include <cstdlib>
#include <new>

#if defined(__SANITIZE_ADDRESS__)
#define CHAT_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CHAT_ASAN 1
#endif
#endif

namespace {

std::size_t counter = 0;

}  // namespace

namespace chat::tools {

std::size_t allocations() noexcept { return counter; }
void reset_allocations() noexcept { counter = 0; }

#if defined(CHAT_ASAN)
bool counting_available() noexcept { return false; }
#else
bool counting_available() noexcept { return true; }
#endif

}  // namespace chat::tools

#if !defined(CHAT_ASAN)

// Замена глобальных операторов. Массивные формы по умолчанию вызывают эти же,
// поэтому их подменять не нужно.
void* operator new(std::size_t size) {
    ++counter;
    if (void* const memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

#endif  // !CHAT_ASAN
