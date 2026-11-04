#include "kvs/owning_buffer.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace kvs {

OwningBuffer::OwningBuffer(const std::string& text)
    : OwningBuffer(text.data(), text.size()) {}

OwningBuffer::OwningBuffer(const char* data, std::size_t size)
    : data_(size == 0 ? nullptr : new char[size]), size_(size) {
    if (size_ != 0) {
        std::copy_n(data, size_, data_);
    }
}

OwningBuffer::OwningBuffer(const OwningBuffer& other)
    : OwningBuffer(other.data_, other.size_) {}

// std::exchange из C++14: забирает значение и оставляет источник в
// определённом состоянии одной операцией.
OwningBuffer::OwningBuffer(OwningBuffer&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, std::size_t{0})) {}

// Copy-and-swap. Сначала строится полная копия, и только потом затрагивается
// состояние *this — обменом, который не бросает исключений.
//
// Отсюда два следствия.
//
// Самоприсваивание безопасно само собой, без проверки this != &other.
// Классическая ошибочная реализация сначала освобождает свой буфер, а потом
// копирует из other — но при a = a это тот же самый, уже освобождённый буфер.
// Здесь копия снимается, пока источник ещё цел; обмен с ней и разрушение
// старого буфера вместе с временным объектом ничего не портят.
//
// Сильная гарантия: если new char[] внутри копирующего конструктора бросит
// std::bad_alloc, *this ещё не тронут и остаётся в прежнем состоянии.
OwningBuffer& OwningBuffer::operator=(const OwningBuffer& other) {
    OwningBuffer copy(other);
    swap(*this, copy);
    return *this;
}

// Перемещающее присваивание не может бросить: оно только обменивается
// указателями. Прежний буфер *this уходит во временный объект и освобождается
// при выходе из функции, источник остаётся пустым и пригодным к повторному
// использованию.
OwningBuffer& OwningBuffer::operator=(OwningBuffer&& other) noexcept {
    OwningBuffer taken(std::move(other));
    swap(*this, taken);
    return *this;
}

OwningBuffer::~OwningBuffer() { delete[] data_; }

void swap(OwningBuffer& a, OwningBuffer& b) noexcept {
    std::swap(a.data_, b.data_);
    std::swap(a.size_, b.size_);
}

// Без noexcept на перемещающем конструкторе std::vector при перевыделении
// выбрал бы копирование — через std::move_if_noexcept, ради сохранения
// сильной гарантии. Проверяем это на этапе компиляции.
static_assert(std::is_nothrow_move_constructible<OwningBuffer>::value,
              "move construction must be noexcept, otherwise vector copies");
static_assert(std::is_nothrow_move_assignable<OwningBuffer>::value,
              "move assignment must be noexcept");

}  // namespace kvs
