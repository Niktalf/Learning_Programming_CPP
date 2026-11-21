#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace taskq {

// Этап 4: тело задачи — иерархия классов вместо std::variant.
//
// Деструктор виртуальный, и это обязательно. Тело удаляется через
// std::unique_ptr<IPayload>, то есть через указатель на базовый класс. Без
// virtual это неопределённое поведение: вызовется только ~IPayload, а
// деструктор наследника — с телом задачи — не выполнится. unique_ptr не
// спасает: он вызывает delete по статическому типу.
//
// На этапе 17 у каждого наследника появится сериализация — там иерархия и
// окупится: новый вид тела добавляется новым классом, без правки всех visit.
class IPayload {
public:
    virtual ~IPayload() = default;

    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
    [[nodiscard]] virtual std::string to_string() const = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    // Виртуальный конструктор копирования. Копировать тело через указатель на
    // базу нельзя: неизвестно, какой класс создавать. На clone держится
    // копирование задачи, а значит и сильная гарантия пакетной постановки
    // этапа 3: она копирует задетые очереди.
    [[nodiscard]] virtual std::unique_ptr<IPayload> clone() const = 0;

    [[nodiscard]] virtual bool equals(const IPayload& other) const noexcept = 0;

protected:
    // Копирование базы — только для наследников, иначе объект обрезался бы.
    IPayload() = default;
    IPayload(const IPayload&) = default;
    IPayload& operator=(const IPayload&) = default;
};

using PayloadPtr = std::unique_ptr<IPayload>;

// Текстовая команда для воркера.
class TextPayload final : public IPayload {
public:
    explicit TextPayload(std::string command) noexcept : command_(std::move(command)) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "text"; }
    [[nodiscard]] std::string to_string() const override { return command_; }
    [[nodiscard]] std::size_t size() const noexcept override { return command_.size(); }
    [[nodiscard]] PayloadPtr clone() const override;
    [[nodiscard]] bool equals(const IPayload& other) const noexcept override;

    [[nodiscard]] const std::string& command() const noexcept { return command_; }

private:
    std::string command_;
};

// Числовой аргумент: num:42. Единственное изменяемое тело — его меняет BUMP.
class NumericPayload final : public IPayload {
public:
    explicit NumericPayload(std::int64_t value) noexcept : value_(value) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "number"; }
    [[nodiscard]] std::string to_string() const override { return std::to_string(value_); }
    [[nodiscard]] std::size_t size() const noexcept override { return sizeof(value_); }
    [[nodiscard]] PayloadPtr clone() const override;
    [[nodiscard]] bool equals(const IPayload& other) const noexcept override;

    [[nodiscard]] std::int64_t value() const noexcept { return value_; }
    void set_value(std::int64_t value) noexcept { value_ = value; }

private:
    std::int64_t value_;
};

// Двоичные данные: bin:cafe.
class BinaryPayload final : public IPayload {
public:
    explicit BinaryPayload(std::string bytes) noexcept : bytes_(std::move(bytes)) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "binary"; }
    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::size_t size() const noexcept override { return bytes_.size(); }
    [[nodiscard]] PayloadPtr clone() const override;
    [[nodiscard]] bool equals(const IPayload& other) const noexcept override;

    [[nodiscard]] const std::string& bytes() const noexcept { return bytes_; }

private:
    std::string bytes_;
};

}  // namespace taskq
