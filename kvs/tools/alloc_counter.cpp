#include "alloc_counter.hpp"

#include <cstdlib>
#include <new>

#if defined(__SANITIZE_ADDRESS__)
#define KVS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define KVS_ASAN 1
#endif
#endif

namespace {

std::size_t counter = 0;

}  // namespace

namespace kvs::tools {

std::size_t allocations() noexcept { return counter; }
void reset_allocations() noexcept { counter = 0; }

#if defined(KVS_ASAN)
bool counting_available() noexcept { return false; }
#else
bool counting_available() noexcept { return true; }
#endif

}  // namespace kvs::tools

#if !defined(KVS_ASAN)

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

#endif  // !KVS_ASAN
