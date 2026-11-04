#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace chat {

// Учебный класс этапа 1, переведённый на std::unique_ptr<char[]> —
// домашнее задание этапа 4.
//
// Было: сырой char*, и все пять специальных функций написаны руками.
// Стало: память держит unique_ptr, и класс перестал быть владельцем сам —
// владеет его поле. Что исчезло и что осталось, разобрано в docs/ownership.md:
//
//   - деструктор исчез: unique_ptr<char[]> сам вызовет delete[];
//   - перемещающее присваивание исчезло: единое присваивание по значению
//     покрывает и копию, и перемещение;
//   - копирующий конструктор остался: unique_ptr не копируется, глубокую
//     копию за нас никто не сделает;
//   - перемещающий конструктор остался, хотя unique_ptr перемещается сам.
//     Причина — инвариант, связывающий два поля: size_ обязан быть нулём,
//     когда буфера нет. Сгенерированное перемещение оставило бы в источнике
//     прежний size_ при пустом указателе, и size() врал бы. Правило нуля
//     работает, когда каждое поле само отвечает за свой инвариант; здесь
//     инвариант общий.
class OwningBuffer {
public:
    OwningBuffer() = default;
    explicit OwningBuffer(const std::string& text);
    OwningBuffer(const char* data, std::size_t size);

    OwningBuffer(const OwningBuffer& other);
    OwningBuffer(OwningBuffer&& other) noexcept;

    // Одно присваивание на оба случая. Параметр по значению: при a = b он
    // строится копированием, при a = std::move(b) — перемещением, и дальше
    // остаётся обменяться с ним. Это copy-and-swap без отдельной копии внутри.
    // noexcept честный: бросить может только построение параметра, а оно
    // происходит у вызывающего, до входа в функцию.
    OwningBuffer& operator=(OwningBuffer other) noexcept;

    [[nodiscard]] const char* data() const noexcept { return data_.get(); }
    [[nodiscard]] char* data() noexcept { return data_.get(); }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::string str() const { return std::string(data_.get(), size_); }

    friend void swap(OwningBuffer& a, OwningBuffer& b) noexcept;

private:
    std::unique_ptr<char[]> data_;
    std::size_t size_ = 0;
};

}  // namespace chat
