#include "kvs/owning_buffer.hpp"

#include <algorithm>
#include <memory>
#include <type_traits>
#include <utility>

namespace kvs {

OwningBuffer::OwningBuffer(const std::string& text) : OwningBuffer(text.data(), text.size()) {}

// make_unique<char[]>(n) обнуляет память, new char[n] — нет. Здесь это
// лишняя работа: память тут же перезаписывается копией. В C++20 для этого
// появился make_unique_for_overwrite; до него приходится либо платить за
// обнуление, либо писать new, а new в рабочем коде этапа 4 запрещён.
OwningBuffer::OwningBuffer(const char* data, std::size_t size)
    : data_(size == 0 ? nullptr : std::make_unique<char[]>(size)), size_(size) {
    if (size_ != 0) {
        std::copy_n(data, size_, data_.get());
    }
}

OwningBuffer::OwningBuffer(const OwningBuffer& other) : OwningBuffer(other.data(), other.size_) {}

// Указатель перемещается сам; руками — только size_, чтобы источник остался
// согласованным: пустой буфер нулевой длины.
OwningBuffer::OwningBuffer(OwningBuffer&& other) noexcept
    : data_(std::move(other.data_)), size_(std::exchange(other.size_, std::size_t{0})) {}

// Самоприсваивание безопасно само собой: при a = a параметр — независимая
// копия, при a = std::move(a) — забранное у a содержимое, которое обмен
// тут же возвращает обратно.
OwningBuffer& OwningBuffer::operator=(OwningBuffer other) noexcept {
    swap(*this, other);
    return *this;
}

void swap(OwningBuffer& a, OwningBuffer& b) noexcept {
    a.data_.swap(b.data_);
    std::swap(a.size_, b.size_);
}

// Без noexcept на перемещении std::vector при перевыделении выбрал бы
// копирование — через std::move_if_noexcept, ради сохранения сильной гарантии.
static_assert(std::is_nothrow_move_constructible_v<OwningBuffer>,
              "move construction must be noexcept, otherwise vector copies");
static_assert(std::is_nothrow_move_assignable_v<OwningBuffer>,
              "move assignment must be noexcept");
// Деструктор не объявлен, но остаётся noexcept: его генерирует компилятор.
static_assert(std::is_nothrow_destructible_v<OwningBuffer>, "destruction must not throw");

}  // namespace kvs
