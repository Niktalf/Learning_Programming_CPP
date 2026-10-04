#include "owning_buffer.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

OwningBuffer::OwningBuffer(const std::string& text)
    :   OwningBuffer(text.data(), text.size()) {}

OwningBuffer::OwningBuffer(const char* data, const std::size_t size)
    :   data_(size == 0 ? nullptr : new char[size]), size_(size) {
    if (size_ != 0) {
        std::copy_n(data, size_, data_);
    }
}

OwningBuffer::OwningBuffer(const OwningBuffer& other)
    :   OwningBuffer(other.data_, other.size_) {}

OwningBuffer::OwningBuffer(OwningBuffer&& other) noexcept
    :   data_(std::exchange(other.data_, nullptr)),
        size_(std::exchange(other.size_, std::size_t{0})) {}

OwningBuffer& OwningBuffer::operator=(OwningBuffer other) noexcept {
    swap(*this, other);
    return *this;
}

OwningBuffer::~OwningBuffer() { delete[] data_; }

void swap(OwningBuffer& a, OwningBuffer& b) noexcept {
    std::swap(a.data_, b.data_);
    std::swap(a.size_, b.size_);
}

static_assert(std::is_nothrow_move_constructible_v<OwningBuffer>, "the move must be noexcept, otherwise the vector will copy");
static_assert(std::is_nothrow_move_assignable_v<OwningBuffer>, "the move assignment operator must be noexcept");
