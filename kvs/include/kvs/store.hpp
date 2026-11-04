#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "kvs/value.hpp"

namespace kvs {

// Хранилище параметризовано типом значения по одной практической причине:
// в тестах вместо Value подставляется инструментированный тип, считающий
// копирования и перемещения. Без этого утверждение «вставка временного
// объекта не копирует» проверить нечем.
template <class V>
class BasicStore {
public:
    void set(std::string key, V value) {
        // insert_or_assign из C++17: одна операция вместо поиска и вставки.
        data_.insert_or_assign(std::move(key), std::move(value));
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

    bool del(std::string_view key) {
        const auto it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        data_.erase(it);
        return true;
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
    Map data_;
};

using Store = BasicStore<Value>;

}  // namespace kvs
