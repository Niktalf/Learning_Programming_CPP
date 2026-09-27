#pragma once

#include <cstddef>
#include <string>

class OwningBuffer {
public:
    OwningBuffer() = default;
    explicit OwningBuffer(const std::string& text);
    OwningBuffer(const char* data, std::size_t size);

    OwningBuffer(const OwningBuffer& other);
    OwningBuffer(OwningBuffer&& other) noexcept;
    OwningBuffer& operator=(OwningBuffer other) noexcept;

    ~OwningBuffer();

    [[nodiscard]] const char* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::string str() const { return std::string(data_, size_); }

    friend void swap(OwningBuffer& a, OwningBuffer& b) noexcept;

private:
    char* data_ = nullptr;
    std::size_t size_ = 0;
};
