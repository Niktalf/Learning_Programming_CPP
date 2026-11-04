#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kvs/errors.hpp"
#include "kvs/value.hpp"

namespace kvs {

// Хранилище параметризовано типом значения по одной практической причине:
// в тестах вместо Value подставляется инструментированный тип, считающий
// копирования и перемещения. Без этого утверждение «вставка временного
// объекта не копирует» проверить нечем.
template <class V>
class BasicStore {
public:
    BasicStore() = default;

    // Этап 3: хранилище с ограничением объёма, как maxmemory у Redis. Объём
    // считается приближённо — длина ключа плюс размер значения, — и этого
    // достаточно, чтобы отказать записи до того, как что-либо изменено.
    explicit BasicStore(std::size_t memory_limit) noexcept : memory_limit_(memory_limit) {}

    // Сильная гарантия: всё, что может бросить, — проверка предела и вставка —
    // происходит до изменения учёта занятой памяти. Бросок оставляет
    // хранилище прежним.
    void set(std::string key, V value) {
        const std::size_t added = footprint(key, value);
        const auto it = data_.find(key);
        const std::size_t removed = it == data_.end() ? 0 : footprint(it->first, it->second);
        const std::size_t next_used = used_ - removed + added;
        require_room(key, next_used);
        // insert_or_assign из C++17: одна операция вместо поиска и вставки.
        data_.insert_or_assign(std::move(key), std::move(value));
        used_ = next_used;
    }

    // Этап 2: вместо сырого указателя — std::optional. Отсутствие значения
    // теперь выражено в типе, и вызывающий не может забыть о проверке:
    // разыменование пустого optional не проходит молча, как обращение
    // по nullptr.
    //
    // Возвращается копия, а не ссылка: std::optional<T&> стандартом не
    // предусмотрен. Для небольшого значения копия приемлема; там, где она
    // была бы дорогой, пришлось бы брать reference_wrapper или указатель,
    // и это стоит помнить как ограничение, а не считать optional бесплатным.
    [[nodiscard]] std::optional<V> get(std::string_view key) const {
        const auto it = data_.find(key);
        if (it == data_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        return data_.find(key) != data_.end();
    }

    // Этап 3: несколько ключей одной операцией с сильной гарантией. Либо
    // записаны все пары, либо хранилище не изменилось вовсе.
    //
    // Схема — copy-and-swap. Всё, что может бросить, делается над копией:
    // копирование таблицы, вставка каждой пары. Исходные данные при этом не
    // затронуты, и бросок на любом шаге оставляет их целыми. В конце копия
    // меняется местами с оригиналом операцией, которая бросить не может.
    // Это точка невозврата: до неё отказ ничего не портит, после неё отказа
    // не бывает.
    //
    // Цена сознательная: полная копия таблицы на каждую операцию. Для
    // настоящего хранилища это неприемлемо, и на этапе 11 с шардами вопрос
    // вернётся. Альтернатива — журнал отката, но она сложнее в разы, и откат
    // сам может бросить.
    void mset(const std::vector<std::pair<std::string, V>>& pairs) {
        BasicStore next = *this;
        for (const auto& [key, value] : pairs) {
            next.set(key, value);   // может бросить: копия, предел объёма
        }
        swap(*this, next);   // noexcept: точка невозврата
    }

    // swap помечен noexcept, и это не косметика. mset строит новое состояние
    // в стороне и последней операцией меняет его местами с текущим. Если бы
    // обмен мог бросить, он перестал бы быть точкой невозврата: бросок на
    // середине обмена оставил бы таблицу от одного состояния, а счётчик
    // занятой памяти — от другого, и сильная гарантия молча выродилась бы
    // в базовую. Обмен std::map и целых не бросает никогда — это и проверяет
    // тест Guarantee.SwapIsNoexcept.
    friend void swap(BasicStore& a, BasicStore& b) noexcept {
        a.data_.swap(b.data_);
        std::swap(a.used_, b.used_);
        std::swap(a.memory_limit_, b.memory_limit_);
    }

    bool del(std::string_view key) {
        const auto it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        used_ -= footprint(it->first, it->second);
        data_.erase(it);
        return true;
    }

    [[nodiscard]] std::size_t used_bytes() const noexcept { return used_; }
    [[nodiscard]] std::optional<std::size_t> memory_limit() const noexcept {
        return memory_limit_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    // Функтор принимается по значению — как это делают алгоритмы стандартной
    // библиотеки. Именно поэтому лямбда с захватом по значению и mutable не
    // выносит изменения наружу: меняется копия.
    //
    // Structured bindings из C++17: пара раскладывается на имена, а не
    // читается через first и second.
    template <class F>
    void for_each(F fn) const {
        for (const auto& [key, value] : data_) {
            fn(key, value);
        }
    }

private:
    // std::less<> включает разнородный поиск: методы принимают string_view
    // и не создают временную строку на каждый вызов.
    using Map = std::map<std::string, V, std::less<>>;

    [[nodiscard]] static std::size_t footprint(const std::string& key, const V& value) noexcept {
        return key.size() + value.size();
    }

    void require_room(const std::string& key, std::size_t needed) const {
        if (memory_limit_ && needed > *memory_limit_) {
            throw OutOfMemoryError(key, needed, *memory_limit_);
        }
    }

    Map data_;
    std::size_t used_ = 0;
    std::optional<std::size_t> memory_limit_;
};

using Store = BasicStore<Value>;

}  // namespace kvs
