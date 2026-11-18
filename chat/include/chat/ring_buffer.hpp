#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace chat {

// Этап 7: история комнаты ограничена ёмкостью, и при переполнении самое
// старое сообщение вытесняется. Это кольцевой буфер на std::vector, и выбран
// он по замерам (docs/benchmarks.md): на профиле «добавление в конец,
// вытеснение из начала, чтение последних N» кольцо в 1,5 раза быстрее deque
// и в 8 раз быстрее list и выделяет память один раз, а не на каждые 64
// сообщения (deque) или на каждое сообщение (list).
//
// Устройство. Место растёт удвоением до ёмкости, как у обычного vector. Когда
// буфер полон, новое сообщение перезаписывает самое старое, и «начало»
// сдвигается по кругу: head_ — физический индекс самого старого. Логический
// индекс i (0 — самое старое) переходит в физический сложением и одним
// сравнением, без остатка от деления: деление — десятки тактов, и замер
// показал шестикратную разницу на одном этом (docs/benchmarks.md).
//
// Номера сообщений в чате абсолютные: /type #general 1500 — тысяча пятисотое
// сообщение комнаты, даже если первые тысяча двести уже вытеснены. evicted()
// — сколько вытеснено, то есть абсолютный номер самого старого.
template <class T>
class RingBuffer {
    template <bool Const>
    class Cursor;

public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = Cursor<false>;
    using const_iterator = Cursor<true>;

    // Звёздочка этапа 7: отдельный тип конца для from_oldest(). Конец — не
    // позиция, а условие «логический индекс дошёл до размера», и итератор
    // проверяет его сам.
    struct End {};

    explicit RingBuffer(size_type capacity) : capacity_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("ring buffer capacity must be positive");
        }
    }

    [[nodiscard]] size_type size() const noexcept { return storage_.size(); }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return storage_.empty(); }
    [[nodiscard]] bool full() const noexcept { return storage_.size() == capacity_; }
    [[nodiscard]] size_type evicted() const noexcept { return evicted_; }

    // Логический индекс: 0 — самое старое из оставшихся. Без проверки
    // границ, как у vector.
    [[nodiscard]] T& operator[](size_type i) noexcept { return storage_[physical(i)]; }
    [[nodiscard]] const T& operator[](size_type i) const noexcept { return storage_[physical(i)]; }

    [[nodiscard]] T& front() noexcept { return (*this)[0]; }
    [[nodiscard]] const T& front() const noexcept { return (*this)[0]; }
    [[nodiscard]] T& back() noexcept { return (*this)[size() - 1]; }
    [[nodiscard]] const T& back() const noexcept { return (*this)[size() - 1]; }

    // Добавление в конец; полный буфер вытесняет самое старое. Сильная
    // гарантия: если буфер не полон, это push_back у vector; если полон —
    // присваивание перемещением, и счётчики меняются после него.
    void push_back(T value) {
        if (!full()) {
            storage_.push_back(std::move(value));
            return;
        }
        storage_[head_] = std::move(value);
        head_ = head_ + 1 == capacity_ ? 0 : head_ + 1;
        ++evicted_;
    }

    // Гарантия, что следующий push_back не выделит память и, значит, при
    // перемещении без исключений не бросит. Нужна рассылке в несколько
    // комнат: фаза фиксации не должна бросать (BasicChatStore::post_many).
    void reserve_one_more() {
        if (!full() && storage_.size() == storage_.capacity()) {
            storage_.reserve(std::min(capacity_, std::max<size_type>(8, storage_.size() * 2)));
        }
    }

    [[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }
    [[nodiscard]] iterator end() noexcept { return iterator(this, size()); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(this, 0); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(this, size()); }

    // Звёздочка: те же элементы, но конец — страж End. Диапазон не common:
    // классические алгоритмы, которым нужна пара итераторов одного типа, с
    // ним не работают, а алгоритмы ranges и range-based for — работают.
    [[nodiscard]] std::ranges::subrange<const_iterator, End> from_oldest() const noexcept {
        return {begin(), End{}};
    }

private:
    // Двунаправленный итератор по логическим индексам. Хранит буфер и индекс,
    // а не указатель на элемент, поэтому перевыделение storage_ его не
    // портит. Что портит — в docs/iterators.md: после вытеснения тот же
    // логический индекс указывает на следующее сообщение.
    //
    // Двунаправленный, а не только прямой: история читается с конца —
    // views::reverse в history_where и newest_first. Произвольный доступ
    // был бы возможен (индекс есть), но в проекте не нужен.
    template <bool Const>
    class Cursor {
        using Buffer = std::conditional_t<Const, const RingBuffer, RingBuffer>;

    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Const, const T&, T&>;
        using pointer = std::conditional_t<Const, const T*, T*>;

        Cursor() = default;

        // Копирование и перемещение объявлены явно, и это не многословие.
        // Конструктор ниже у Cursor<false> имеет сигнатуру копирующего —
        // Cursor(const Cursor<false>&), — и хотя requires его выключает,
        // объявлен он пользователем. Пользовательский копирующий
        // конструктор отменяет неявный перемещающий, а выключенный не
        // заменяет копирующий: итератор становится некопируемым и
        // неперемещаемым, и static_assert(forward_iterator) не проходит.
        Cursor(const Cursor&) = default;
        Cursor(Cursor&&) = default;
        Cursor& operator=(const Cursor&) = default;
        Cursor& operator=(Cursor&&) = default;

        // Изменяемый итератор приводится к константному, как у
        // стандартных контейнеров.
        Cursor(const Cursor<false>& other) noexcept
            requires Const
            : buffer_(other.buffer_), index_(other.index_) {}

        [[nodiscard]] reference operator*() const noexcept { return (*buffer_)[index_]; }
        [[nodiscard]] pointer operator->() const noexcept { return &(*buffer_)[index_]; }

        Cursor& operator++() noexcept {
            ++index_;
            return *this;
        }
        Cursor operator++(int) noexcept {
            Cursor before = *this;
            ++index_;
            return before;
        }
        Cursor& operator--() noexcept {
            --index_;
            return *this;
        }
        Cursor operator--(int) noexcept {
            Cursor before = *this;
            --index_;
            return before;
        }

        [[nodiscard]] friend bool operator==(const Cursor&, const Cursor&) = default;

        // Сравнение со стражем: конец — когда индекс дошёл до размера.
        [[nodiscard]] friend bool operator==(const Cursor& it, End) noexcept {
            return it.index_ == it.buffer_->size();
        }

        // Логический индекс — номер внутри буфера, 0 — самое старое.
        [[nodiscard]] size_type index() const noexcept { return index_; }

    private:
        friend class RingBuffer;
        friend class Cursor<!Const>;
        Cursor(Buffer* buffer, size_type index) noexcept : buffer_(buffer), index_(index) {}

        Buffer* buffer_ = nullptr;
        size_type index_ = 0;
    };

    [[nodiscard]] size_type physical(size_type i) const noexcept {
        const size_type at = head_ + i;
        return at < storage_.size() ? at : at - storage_.size();
    }

    std::vector<T> storage_;
    size_type capacity_;
    size_type head_ = 0;      // физический индекс самого старого; 0, пока не полон
    size_type evicted_ = 0;   // сколько сообщений вытеснено за всё время
};

}  // namespace chat
