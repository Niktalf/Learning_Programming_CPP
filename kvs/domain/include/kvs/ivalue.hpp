#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
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

    // Этап 5: сериализация, чтобы тип удовлетворял концепции Serializable.
    [[nodiscard]] std::vector<std::byte> to_bytes() const;
    [[nodiscard]] static StringValue from_bytes(std::span<const std::byte> bytes);

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

    [[nodiscard]] std::vector<std::byte> to_bytes() const;
    [[nodiscard]] static IntValue from_bytes(std::span<const std::byte> bytes);

private:
    std::int64_t data_;
};

// Список строк. Этап 7: полный набор операций — вставка и снятие с обоих
// концов и диапазон по индексам, как LPUSH, RPUSH, LPOP, RPOP и LRANGE у
// Redis.
//
// Контейнер — std::deque, и выбран он по замерам (docs/benchmarks.md). На
// профиле «много вставок в оба конца, редкое чтение по индексу» deque быстрее
// list в 2–10 раз и быстрее vector в тысячи раз на 10^5 элементов: у vector
// вставка в начало сдвигает все элементы, у list каждое чтение по индексу —
// проход от начала, а каждый элемент — отдельное выделение памяти. deque
// вставляет в оба конца за O(1), читает по индексу за O(1) и выделяет память
// блоками.
class ListValue final : public IValue {
public:
    ListValue() = default;

    [[nodiscard]] std::string_view type_name() const noexcept override { return "list"; }
    [[nodiscard]] std::string to_string() const override;
    // Объём — сумма длин элементов; число элементов отдаёт length().
    [[nodiscard]] std::size_t size() const noexcept override { return bytes_; }
    [[nodiscard]] std::unique_ptr<IValue> clone() const override;
    [[nodiscard]] bool equals(const IValue& other) const noexcept override;

    // Вставка с сильной гарантией: push_front и push_back у deque при
    // исключении не меняют контейнер. Учёт объёма — после вставки.
    void push_front(std::string item);
    void push_back(std::string item);

    // Снятие с конца. Предусловие — список не пуст; проверяет вызывающий.
    [[nodiscard]] std::string pop_front();
    [[nodiscard]] std::string pop_back();

    // Элементы с индексами от start до stop включительно, как LRANGE у
    // Redis: отрицательный индекс считается от конца (-1 — последний),
    // выход за границы обрезается, пустой диапазон — пустой результат.
    [[nodiscard]] std::vector<std::string> range(std::int64_t start, std::int64_t stop) const;

    [[nodiscard]] std::size_t length() const noexcept { return items_.size(); }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
    [[nodiscard]] const std::deque<std::string>& items() const noexcept { return items_; }

private:
    std::deque<std::string> items_;
    std::size_t bytes_ = 0;
};

}  // namespace kvs
