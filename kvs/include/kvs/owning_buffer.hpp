#pragma once

#include <cstddef>
#include <string>

namespace kvs {

// Учебная противоположность классу Value. Этот класс владеет сырой памятью,
// поэтому правило нуля здесь не работает и все пять специальных функций
// приходится писать руками: деструктор, копирующий конструктор, копирующее
// присваивание, перемещающий конструктор и перемещающее присваивание.
//
// В проекте он больше нигде не используется: он нужен ровно затем, чтобы
// один раз увидеть, что именно компилятор делает за вас в остальных классах.
class OwningBuffer {
public:
    OwningBuffer() = default;
    explicit OwningBuffer(const std::string& text);
    OwningBuffer(const char* data, std::size_t size);

    OwningBuffer(const OwningBuffer& other);
    OwningBuffer(OwningBuffer&& other) noexcept;

    // Копирующее присваивание через copy-and-swap. Почему это решает
    // самоприсваивание — в комментарии к определению.
    OwningBuffer& operator=(const OwningBuffer& other);
    OwningBuffer& operator=(OwningBuffer&& other) noexcept;

    ~OwningBuffer();

    const char* data() const noexcept { return data_; }
    char* data() noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    std::string str() const { return std::string(data_, size_); }

    friend void swap(OwningBuffer& a, OwningBuffer& b) noexcept;

private:
    char* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace kvs
