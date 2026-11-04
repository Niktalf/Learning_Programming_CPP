#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kvs {

// Этап 4: значение — иерархия классов вместо std::variant.
//
// Деструктор виртуальный, и это обязательно. Объекты удаляются через
// std::unique_ptr<IValue>, то есть через указатель на базовый класс. Без
// virtual это неопределённое поведение: вызовется только ~IValue, а
// деструктор наследника со всеми его полями не выполнится. unique_ptr здесь
// не спасает — он вызывает delete по статическому типу, то есть делает ту же
// ошибку, просто автоматически.
//
// Чем иерархия хуже variant, видно сразу: каждое значение — отдельная
// аллокация, а проверка типа — dynamic_cast вместо проверки при компиляции.
// Чем лучше: новый тип добавляется новым классом, без правки всех visit.
// На этапе 7 типов станет больше, на этапе 17 появится сериализация — там
// это и окупится.
class IValue {
public:
    virtual ~IValue() = default;

    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
    [[nodiscard]] virtual std::string to_string() const = 0;

    // Приблизительный объём в байтах — для учёта памяти хранилища.
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    // Виртуальный конструктор копирования. Копировать объект через указатель
    // на базу нельзя: неизвестно, какой класс создавать. Каждый наследник
    // знает свой тип и создаёт копию себя. На этом держится копирование
    // хранилища — а значит и сильная гарантия MSET из этапа 3.
    [[nodiscard]] virtual std::unique_ptr<IValue> clone() const = 0;

    [[nodiscard]] virtual bool equals(const IValue& other) const noexcept = 0;

protected:
    // Копирование и перемещение базы доступны только наследникам — для их
    // собственных специальных функций. Снаружи скопировать IValue по
    // значению нельзя: это обрезало бы объект до базы.
    IValue() = default;
    IValue(const IValue&) = default;
    IValue(IValue&&) = default;
    IValue& operator=(const IValue&) = default;
    IValue& operator=(IValue&&) = default;
};

// Наследники помечены final: дальше их не наследуют, и компилятор может
// вызывать их методы без виртуальной диспетчеризации, когда тип известен.

class StringValue final : public IValue {
public:
    explicit StringValue(std::string data) noexcept : data_(std::move(data)) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "string"; }
    [[nodiscard]] std::string to_string() const override { return data_; }
    [[nodiscard]] std::size_t size() const noexcept override { return data_.size(); }
    [[nodiscard]] std::unique_ptr<IValue> clone() const override;
    [[nodiscard]] bool equals(const IValue& other) const noexcept override;

    [[nodiscard]] const std::string& data() const noexcept { return data_; }

private:
    std::string data_;
};

class IntValue final : public IValue {
public:
    explicit IntValue(std::int64_t data) noexcept : data_(data) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "int"; }
    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::size_t size() const noexcept override { return sizeof(data_); }
    [[nodiscard]] std::unique_ptr<IValue> clone() const override;
    [[nodiscard]] bool equals(const IValue& other) const noexcept override;

    [[nodiscard]] std::int64_t data() const noexcept { return data_; }

private:
    std::int64_t data_;
};

// Список строк. Пока умеет только добавление в конец и длину; полный набор
// операций появится на этапе 7 вместе с выбором контейнера.
class ListValue final : public IValue {
public:
    ListValue() = default;

    [[nodiscard]] std::string_view type_name() const noexcept override { return "list"; }
    [[nodiscard]] std::string to_string() const override;
    // Объём — сумма длин элементов; число элементов отдаёт length().
    [[nodiscard]] std::size_t size() const noexcept override { return bytes_; }
    [[nodiscard]] std::unique_ptr<IValue> clone() const override;
    [[nodiscard]] bool equals(const IValue& other) const noexcept override;

    void push_back(std::string item);
    [[nodiscard]] std::size_t length() const noexcept { return items_.size(); }
    [[nodiscard]] const std::vector<std::string>& items() const noexcept { return items_; }

private:
    std::vector<std::string> items_;
    std::size_t bytes_ = 0;
};

}  // namespace kvs
