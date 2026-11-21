#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "kvs/ivalue.hpp"

namespace kvs {

// Значение с семантикой значения поверх полиморфного объекта.
//
// Внутри — std::unique_ptr<IValue>: объект в куче, владелец один. Снаружи —
// обычный копируемый тип, как на этапах 1–3. Благодаря этому хранилище и
// сильная гарантия MSET из этапа 3 остались без изменений: таблица
// по-прежнему копируется, просто копирование значения теперь идёт через
// clone. Тесты этапа 3 проходят без правок, а тест Guarantee.MsetWithPolymorphicValues
// проверяет то же на настоящих значениях с бросающим clone.
//
// Здесь правило нуля перестаёт работать, и это стоит увидеть. Единственное
// поле — unique_ptr — не копируется, значит компилятор не сгенерирует
// копирующие операции. Их приходится написать: глубокая копия через clone.
// Перемещение генерируется правильно — передача владения — и объявлено через
// = default. Деструктор тоже по умолчанию: unique_ptr сам удалит объект через
// виртуальный деструктор.
class Value {
public:
    // Пустая строка. Конструктор по умолчанию нужен контейнерам и тестам.
    Value() : impl_(std::make_unique<StringValue>(std::string())) {}

    explicit Value(std::string data) : impl_(std::make_unique<StringValue>(std::move(data))) {}
    explicit Value(std::string_view data) : Value(std::string(data)) {}
    explicit Value(std::int64_t data) : impl_(std::make_unique<IntValue>(data)) {}
    explicit Value(std::unique_ptr<IValue> impl) noexcept : impl_(std::move(impl)) {}

    Value(const Value& other) : impl_(other.impl_ ? other.impl_->clone() : nullptr) {}

    // copy-and-swap, как у OwningBuffer на этапе 1: копия делается до того,
    // как затрагивается состояние, поэтому самоприсваивание безопасно само,
    // а бросок clone оставляет *this прежним.
    Value& operator=(const Value& other) {
        Value copy(other);
        swap(*this, copy);
        return *this;
    }

    Value(Value&&) noexcept = default;
    Value& operator=(Value&&) noexcept = default;
    ~Value() = default;

    friend void swap(Value& a, Value& b) noexcept { a.impl_.swap(b.impl_); }

    // Перемещённое значение пусто: impl_ == nullptr. Методы ниже это
    // учитывают — перемещённый объект обязан оставаться пригодным к вызову
    // методов без предусловий, а не только к разрушению. Альтернатива —
    // оставлять при перемещении пустую строку — потребовала бы аллокации,
    // и перемещение перестало бы быть noexcept.
    [[nodiscard]] std::string_view type_name() const noexcept {
        return impl_ ? impl_->type_name() : std::string_view("none");
    }
    [[nodiscard]] std::string to_string() const { return impl_ ? impl_->to_string() : std::string(); }
    [[nodiscard]] std::size_t size() const noexcept { return impl_ ? impl_->size() : 0; }

    // Доступ к конкретному типу: указатель или nullptr, если тип другой.
    // Роль, которую на этапе 2 играл std::get_if у variant; внутри теперь
    // dynamic_cast — проверка во время выполнения, а не при компиляции.
    template <class T>
    [[nodiscard]] const T* as() const noexcept {
        return dynamic_cast<const T*>(impl_.get());
    }

    template <class T>
    [[nodiscard]] T* as() noexcept {
        return dynamic_cast<T*>(impl_.get());
    }

    friend bool operator==(const Value& a, const Value& b) noexcept {
        if (!a.impl_ || !b.impl_) {
            return !a.impl_ && !b.impl_;
        }
        return a.impl_->equals(*b.impl_);
    }
    friend bool operator!=(const Value& a, const Value& b) noexcept { return !(a == b); }

private:
    std::unique_ptr<IValue> impl_;
};

}  // namespace kvs
