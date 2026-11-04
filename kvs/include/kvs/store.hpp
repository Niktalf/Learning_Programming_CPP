#pragma once

#include <cstddef>
#include <map>
#include <string>
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
        // insert_or_assign появится в C++17; пока обходимся find и вставкой.
        typename Map::iterator it = data_.find(key);
        if (it != data_.end()) {
            it->second = std::move(value);
        } else {
            data_.insert(std::make_pair(std::move(key), std::move(value)));
        }
    }

    // Возврат сырого указателя на этом этапе намеренный: nullptr означает
    // «ключа нет». Интерфейс неудобен — вызывающий обязан помнить о проверке,
    // а тип ничего об этом не сообщает. На этапе 2 он заменится на
    // std::optional, и разницу стоит увидеть своими глазами.
    const V* get(const std::string& key) const noexcept {
        typename Map::const_iterator it = data_.find(key);
        return it == data_.end() ? nullptr : &it->second;
    }

    bool del(const std::string& key) {
        typename Map::iterator it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        data_.erase(it);
        return true;
    }

    std::size_t size() const noexcept { return data_.size(); }
    bool empty() const noexcept { return data_.empty(); }

    // Функтор принимается по значению — как это делают алгоритмы стандартной
    // библиотеки. Именно поэтому лямбда с захватом по значению и mutable не
    // выносит изменения наружу: меняется копия.
    //
    // Разложение пары на имена (structured bindings) появится в C++17, пока
    // обращаемся к first и second.
    template <class F>
    void for_each(F fn) const {
        for (const auto& entry : data_) {
            fn(entry.first, entry.second);
        }
    }

private:
    using Map = std::map<std::string, V>;
    Map data_;
};

using Store = BasicStore<Value>;

}  // namespace kvs
